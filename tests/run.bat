@echo off
rem tests/run.bat -- build and run one znet suite with MSVC.
rem
rem   tests\run.bat tcp                     all tcp tests, native backend
rem   tests\run.bat tcp tcp_connect         only tests starting with tcp_connect
rem   tests\run.bat udp -select             select backend
rem   tests\run.bat tcp -select tcp_send    both, one test
rem
rem Works from a plain ssh/cmd session: misc\msvc-env.bat locates Visual
rem Studio and loads the MSVC environment when cl.exe is not on PATH.  lua
rem still has to be on PATH.
rem
rem Without arguments the test binary runs every test in the suite in one
rem process (a failed assert ends the run); named tests run one at a time.
setlocal enabledelayedexpansion

set "SRC=%~dp0"
set "ROOT=%SRC%.."
set "BUILD=%SRC%build\msvc"
set "GEN=%ROOT%\misc\gen_entries.lua"

if "%~1"=="" (
    echo usage: %~nx0 ^<suite^> [-select] [test name...]
    exit /b 2
)
call "%~dp0..\misc\msvc-env.bat"
if errorlevel 1 (
    echo run.bat: cl.exe not found; install the MSVC C++ build tools.
    exit /b 2
)
where lua >nul 2>&1
if errorlevel 1 (
    echo run.bat: lua is not on PATH; misc\gen_entries.lua generates the runner.
    exit /b 2
)

set "S=%~1"
shift
if /i "%S:~-5%"=="_test" set "S=%S:~0,-5%"
set "S=%S%_test"

set "TAG=native"
set "DEFS="
set "ARGS="
:args
if "%~1"=="" goto :args_done
if /i "%~1"=="-select" (
    set "TAG=select"
    set "DEFS=-DZN_USE_SELECT"
) else (
    set "ARGS=%ARGS% %~1"
)
shift
goto :args
:args_done

lua "%GEN%" "%SRC%%S%.c" "%SRC%%S%.gen.inc" -D_WIN32 %DEFS%
if errorlevel 1 exit /b 1
if not exist "%BUILD%\%TAG%" mkdir "%BUILD%\%TAG%"
pushd "%BUILD%\%TAG%"
cl /nologo /W4 /WX /I"%ROOT%" /I"%SRC%/." %DEFS% "%SRC%%S%.c"
if errorlevel 1 (
    popd
    exit /b 1
)
popd

"%BUILD%\%TAG%\%S%.exe" %ARGS%
if not "!errorlevel!"=="0" exit /b 1
exit /b 0
