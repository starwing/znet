-- tests/test.lua -- Lua binding smoke test (Lua >= 5.3).
--
-- Self-contained: it starts a loopback echo server with the binding, connects
-- a client to it in the same state, and checks the echoed bytes.  The old
-- root test.lua fetched www.w3.org; a test must not depend on the network, so
-- only the localhost DNS path is still external-looking (it resolves locally).
--
-- Driven by `just test-lua`, which builds znet.so from zn_lua.c first.
-- Override the port range with ZNET_LUA_TEST_PORT (first port tried).

local znet = require "znet".new()

local failures = 0
local echoed   = false
local done     = false

local function fail(msg)
    io.stderr:write("test.lua: " .. tostring(msg) .. "\n")
    failures = failures + 1
end

-- 1. async DNS: localhost must resolve without touching the network
znet:getaddrinfo("localhost", "http", function(addrs, err)
    if not addrs then
        fail("getaddrinfo(localhost): " .. tostring(err))
    elseif #addrs == 0 then
        fail("getaddrinfo(localhost): no addresses")
    end
end)

-- 2. loopback echo: accept -> recv -> send, client -> send -> recv
local listener
local timer
local client
local port

local function on_accept(self, tcp)
    tcp:receive(function(_, data)
        tcp:send(data):close()
    end)
    return true                 -- re-arm accept for the next connection
end

local first = tonumber(os.getenv("ZNET_LUA_TEST_PORT") or "19023")
for p = first, first + 20 do
    local a = znet:accept(on_accept)
    if a:listen("127.0.0.1", p) then
        listener = a
        port = p
        break
    end
    a:delete()
end
if not listener then
    fail("cannot bind a loopback port in " .. first .. ".." .. (first + 20))
    os.exit(1)
end

client = znet:tcp("127.0.0.1", port, function(self, err)
    if err then
        fail("connect: " .. tostring(err))
        return
    end
    self:send("hello lua"):receive(function(_, data)
        if data ~= "hello lua" then
            fail("echo mismatch: " .. tostring(data))
        else
            echoed = true
        end
        done = true
        client:close()
        listener:close()
        timer:cancel()
    end)
end)

timer = znet:timer(function(self)
    if not done then
        fail("timeout waiting for the loopback echo")
        os.exit(1)
    end
    return false
end)
timer:start(2000)

znet:run()

if not echoed then
    fail("loop finished without the echo")
end
if failures == 0 then
    print("test.lua: OK")
end
os.exit(failures == 0 and 0 or 1)
