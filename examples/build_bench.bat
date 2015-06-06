gcc -s -O3 -DCLIENT bench_echo.c -o bench_client -lws2_32 -lwinmm
gcc -s -O3 -DSERVER bench_echo.c -o bench_server -lws2_32 -lwinmm
pause
