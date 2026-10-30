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

-- 2. timer re-arm vector: a numeric return re-arms with that delay, until the
-- callback returns false.  Exercises the integer branch of lzn_ontimer, which
-- used to return without popping its Lua stack slot (U9).  Nothing else is
-- registered here, so the loop ends when the timer stops.
local listener
local timer
local client
local port

-- 3. loopback echo: accept -> recv -> send, client -> send -> recv
local ticks = 0
local ticktimer = znet:timer(function(self)
    ticks = ticks + 1
    if ticks < 3 then
        return 5                    -- numeric return: re-arm in 5 ms
    end
    return false
end)
ticktimer:start(5)
znet:run()
ticktimer:delete()
if ticks ~= 3 then
    fail("timer numeric re-arm: expected 3 ticks, got " .. tostring(ticks))
end

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

-- 4. U10: returning false from onaccept stops accepting and closes the
-- listening socket.  Leaving it armed-less kept the level-triggered backends
-- spinning, so "false" must close; a later connect to that port then fails
-- instead of sitting in a listener nobody polls for.
local stop_listener
local stop_port
local stop_accepted = false
local stop_refused  = false
local stop_attempts = 0

local function on_accept_stop(_, tcp)
    tcp:delete()
    stop_accepted = true
    return false
end

for p = first + 21, first + 40 do
    local a = znet:accept(on_accept_stop)
    if a:listen("127.0.0.1", p) then
        stop_listener = a
        stop_port = p
        break
    end
    a:delete()
end
if not stop_listener then
    fail("cannot bind a second loopback port")
    os.exit(1)
end

local stop_client = znet:tcp("127.0.0.1", stop_port, function(_, err)
    if err then
        fail("U10: first connect failed: " .. tostring(err))
    end
end)

local stop_probe = znet:timer(function(self)
    stop_attempts = stop_attempts + 1
    if not stop_accepted then
        if stop_attempts > 200 then
            fail("U10: the accept callback never ran")
            return false
        end
        return 10
    end
    stop_client:delete()
    znet:tcp("127.0.0.1", stop_port, function(_, err)
        if err then
            stop_refused = true
        else
            fail("U10: a stopped listener still accepted a connection")
        end
    end)
    return false
end)
stop_probe:start(10)

-- 5. UDP: bind + sendto + receivefrom through the Lua factory.  The
-- constructor used to reject its own address argument, so nothing in this
-- file had ever reached the UDP path (notes/lifecycle-v2.md §9.2).
local udp_srv, udp_cli, udp_timer
local udp_port
local udp_data
local udp_done = false

for p = first + 41, first + 60 do
    local u = znet:udp("127.0.0.1", p)
    if u then
        udp_srv = u
        udp_port = p
        break
    end
end
if not udp_srv then
    fail("cannot bind a loopback UDP port")
    os.exit(1)
end
udp_cli = znet:udp("127.0.0.1", 0)
if not udp_cli then
    fail("cannot bind the UDP client")
    os.exit(1)
end

udp_srv:receivefrom(function(_, data)
    udp_data = data
    udp_done = true
    if udp_timer then udp_timer:cancel() end
    return false                -- stop receiving: one datagram is enough
end)
if not udp_cli:sendto("127.0.0.1", udp_port, "ping") then
    fail("udp sendto failed")
end

udp_timer = znet:timer(function(self)
    if not udp_done then
        fail("timeout waiting for the UDP datagram")
        os.exit(1)
    end
    return false
end)
udp_timer:start(2000)

znet:run()

if not echoed then
    fail("loop finished without the echo")
end
if not stop_refused then
    fail("U10: connecting to a stopped listener did not fail")
end
if not udp_done or udp_data ~= "ping" then
    fail("UDP loopback: done=" .. tostring(udp_done) ..
         " data=" .. tostring(udp_data))
end
udp_srv:delete()
udp_cli:delete()
if failures == 0 then
    print("test.lua: OK")
end
os.exit(failures == 0 and 0 or 1)
