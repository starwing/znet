local znet = require "znet".new()

znet:getaddrinfo("www.w3.org", "http", function(ip, port)
   if not ip then print(ip, port); return end
   for k, v in ipairs(ip) do
      print(k, v, port)
   end
end)

znet:tcp("www.w3.org", "http", function(self, err)
   if err then print(err); return end
   self:send("GET / HTTP/1.1\r\n"..
             "Host: www.w3.org\r\n"..
             "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"..
             "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_5) AppleWebKit/601.6.17 (KHTML, like Gecko) Version/9.1.1 Safari/601.6.17\r\n"..
             "Connection: close\r\n"..
             "\r\n")
   :receive(function(_, s)
       print(s)
   end):onerror(function(tcp, err2)
      print(tcp, err2)
   end)
end)

znet:run()
