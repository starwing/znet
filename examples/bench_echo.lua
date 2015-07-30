package.cpath = "../?.dll;"..package.cpath
local loop = require "znet".new()

local recv_count, recv_bytes = 0, 0
local send_count, send_bytes = 0, 0

loop:accept(function(self, tcp)
   tcp:receive(function(self, s)
      recv_count = recv_count + 1
      recv_bytes = recv_bytes + #s
      self:send(s)
   end)
end):listen(nil, 12345)

local data = ("."):rep(1024)

loop:tcp("127.0.0.1", 12345, function(self, err)
   if err then print(err) return end
   self:send(data)
   :receive(function(self, s)
      send_count = send_count + 1
      send_bytes = send_bytes + #s
      self:send(s)
   end)
end)

loop:timer(function(self)
   print(("send: %d/%d recv: %d/%d"):format(
   send_count, send_bytes,
   recv_count, recv_bytes))
   send_count, send_bytes = 0, 0
   recv_count, recv_bytes = 0, 0
   return true
end):start(1000)

loop:run()
