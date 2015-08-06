@start "Znet Buffered Echo Server" "%~dp0bench_echo_buffer.exe" server 127.0.0.1 8581
@ping 1 -n 2 -w 500 >nul
@"%~dp0bench_echo_buffer.exe" client 127.0.0.1 8581
