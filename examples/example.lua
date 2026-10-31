-- examples/example.lua -- Lua binding demo (Lua >= 5.3).
--
-- Mirror of example.c / example.cpp: a loopback echo server, three clients and
-- one UDP ping.  The accept handler stops the listener after the last client,
-- so znet:run() (ZN_RUN_LOOP) returns as soon as nothing is in flight; the
-- demo needs no exit flag.
--
-- Build znet.so from zn_lua.c first (see README.md) or run `just example-lua`
-- from the repo root.  Set ZNET_EXAMPLE_PORT to change the first TCP port.

local znet = require "znet".new()

local KCLIENTS = 3
local failures = 0
local accepted = 0
local clients = {}
local listener, port
local udpServer, udpClient

local function fail(msg)
    io.stderr:write("example.lua: " .. tostring(msg) .. "\n")
    failures = failures + 1
end

-- tcp echo server: every connection is its own object
local function on_accept(_, tcp)
    tcp:receive(function(self, data)
        self:send(data):close()
    end)
    accepted = accepted + 1
    if accepted < KCLIENTS then
        return true -- re-arm accept
    end
    return false -- the binding closes the listener
end

local firstPort = tonumber(os.getenv("ZNET_EXAMPLE_PORT") or "19023")
for p = firstPort, firstPort + 20 do
    local a = znet:accept(on_accept)
    if a:listen("127.0.0.1", p) then
        listener, port = a, p
        break
    end
    a:delete()
end
if not listener then
    fail("cannot bind a loopback TCP port in " .. firstPort .. ".." ..
         (firstPort + 20))
    os.exit(1)
end

-- three clients: connect, send, check the echo, close
for i = 1, KCLIENTS do
    local hello = "hello from client " .. i
    local c, err = znet:tcp("127.0.0.1", port, function(self, err2)
        if err2 then
            fail("client " .. i .. ": " .. tostring(err2))
            return
        end
        self:send(hello):receive(function(sock, data)
            if data ~= hello then
                fail("client " .. i .. ": echo mismatch: " .. tostring(data))
            end
            sock:close()
        end)
    end)
    if c then
        clients[i] = c
    else
        fail("client " .. i .. ": " .. tostring(err))
    end
end

-- udp ping: the server echoes the datagram back to its sender
local udpPort
for p = firstPort + 41, firstPort + 60 do
    local u = znet:udp("127.0.0.1", p)
    if u then
        udpServer, udpPort = u, p
        break
    end
end
if not udpServer then
    fail("cannot bind a loopback UDP port")
    os.exit(1)
end
udpClient = znet:udp("127.0.0.1", 0)
if not udpClient then
    fail("cannot bind the UDP client")
    os.exit(1)
end

local udpOk = udpServer:receivefrom(function(_, data, addr, fromPort)
    udpServer:sendto(addr, fromPort, data)
    return false -- one datagram is enough
end) and udpClient:receivefrom(function(_, data)
    if data ~= "hello udp" then
        fail("udp echo mismatch: " .. tostring(data))
    end
    return false
end)
if not udpOk then
    fail("cannot arm the UDP exchange")
    udpServer:delete()
    udpClient:delete()
elseif not udpClient:sendto("127.0.0.1", udpPort, "hello udp") then
    fail("udp sendto failed")
    udpServer:delete()
    udpClient:delete()
end

znet:run() -- returns when nothing is in flight

udpServer:delete()
udpClient:delete()

if failures == 0 then
    print("example.lua: OK")
end
os.exit(failures == 0 and 0 or 1)
