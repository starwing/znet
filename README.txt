znet - a simple C network library
---------------------------------

This library is inspired by Yawei Zhang's ZsummerX[1] library, but this one is
a pure C version. Now it implement a IOCP (IO Completion Port) backend on
Windows, and a epoll backend on Linux, a kqueue backend on BSD and Mac OS X
systems, and select backend for others.

The ZNet library itself is a single-header library[2], and have some utils
single-header libraries for common usages. all liraries are standalone and can
used without other headers.

See test.c for examples, and you can see more things in examples folder.

Utils:

zn_buffer.h: mainly based the luaL_Buffer in Lua code. But the interface is
targeted by send/recv method, so it's very useful to make znet send data
continually and receive packets by packets.

zn_deque.h: a double-direct pipe implement that can be used a queue or stack
cross threads. it implements block API like push/pop from deque, and non-block
API to fetch the front and back items from deque.

zn_task.h: a task thread pool implement. you can create a task pool and post
tasks to pool, task will run in other thread, you can use zn_deque.h to notice
original thread the completion of tasks.

The znet library and utils have the same license when Lua[3], have fun :)


[1]: https://github.com/zsummer/zsummerX
[2]: https://github.com/nothings/stb
[3]: https://www.lua.org/license.html

