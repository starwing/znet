@start "Znet Echo Server" "%~dp0bench_echo.exe" server 127.0.0.1 8581
@ping 1 -n 2 -w 500 >nul
@"%~dp0bench_echo.exe" client 127.0.0.1 8581
