@echo off
rem bench\run.bat -- run one server+client benchmark pair for N seconds.
rem
rem   bench\run.bat echo 5           stdio-free echo
rem   bench\run.bat flood 5          flood (send as fast as possible)
rem   bench\run.bat echo-buffer 5    echo through zn_buffer.h framing
rem
rem The pair is killed with taskkill after N seconds, mirroring the POSIX
rem recipe (the programs only stop cleanly on Ctrl+C).
setlocal

set "SRC=%~dp0"
set "BUILD=%SRC%build\msvc"
set "BIN="
if /i "%~1"=="echo"        set "BIN=bench_echo.exe"
if /i "%~1"=="flood"       set "BIN=bench_flood.exe"
if /i "%~1"=="echo-buffer" set "BIN=bench_echo_buffer.exe"

if "%BIN%"=="" (
    echo usage: %~nx0 echo^|flood^|echo-buffer [seconds]
    exit /b 2
)

set "SECS=%~2"
if "%SECS%"=="" set "SECS=5"
set "PORT=%ZNET_BENCH_PORT%"
if "%PORT%"=="" set "PORT=18581"

if not exist "%BUILD%\%BIN%" call "%SRC%build.bat"
if errorlevel 1 exit /b 1

start "znet-bench-server" /b "%BUILD%\%BIN%" server 127.0.0.1 %PORT%
rem ping, not timeout: timeout refuses a redirected stdin (ssh / CI)
ping -n 2 127.0.0.1 >nul
start "znet-bench-client" /b "%BUILD%\%BIN%" client 127.0.0.1 %PORT%
set /a WAIT=%SECS%+1
ping -n %WAIT% 127.0.0.1 >nul
taskkill /f /im "%BIN%" >nul 2>&1
exit /b 0
