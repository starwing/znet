@echo off
rem tests/check.bat -- build and run the znet test suite with MSVC.
rem
rem Works from a plain ssh/cmd session: misc\msvc-env.bat locates Visual
rem Studio and loads the MSVC environment when cl.exe is not on PATH.  lua
rem still has to be on PATH.
rem
rem   tests\check.bat           native backend (IOCP)
rem   tests\check.bat native    same, kept for symmetry with POSIX
rem   tests\check.bat select    rejected: znet.h always uses IOCP on _WIN32
rem
rem The suite list is every tests\*_test.c, so it cannot drift from the POSIX
rem runner.  Every test runs in its own process, so one abort does not hide
rem the rest of the suite.
setlocal enabledelayedexpansion

set "SRC=%~dp0"
set "ROOT=%SRC%.."
set "BUILD=%SRC%build\msvc"
set "GEN=%ROOT%\misc\gen_entries.lua"

call "%~dp0..\misc\msvc-env.bat"
if errorlevel 1 (
    echo check.bat: cl.exe not found; install the MSVC C++ build tools.
    exit /b 2
)
where lua >nul 2>&1
if errorlevel 1 (
    echo check.bat: lua is not on PATH; misc\gen_entries.lua generates the runners.
    exit /b 2
)

if not exist "%BUILD%" mkdir "%BUILD%"

if "%~1"=="" goto :native
if /i "%~1"=="native" goto :native
if /i "%~1"=="select" goto :no_select
echo usage: %~nx0 [native]
exit /b 2

:native
call :build native ""
if errorlevel 1 exit /b 1
call :run native
if errorlevel 1 exit /b 1
echo all suites passed
exit /b 0

:no_select
echo check.bat: the select backend is POSIX-only; znet.h uses IOCP on _WIN32.
exit /b 2

:build
rem %1 = tag, %2 = extra defines for lua and cl
set "TAG=%~1"
set "DEFS=%~2"
if not exist "%BUILD%\%TAG%" mkdir "%BUILD%\%TAG%"
for %%f in ("%SRC%*_test.c") do (
    lua "%GEN%" "%%~ff" "%SRC%%%~nf.gen.inc" -D_WIN32 %DEFS%
    if errorlevel 1 exit /b 1
    pushd "%BUILD%\%TAG%"
    cl /nologo /W4 /WX /I"%ROOT%" /I"%SRC%/." %DEFS% "%%~ff"
    if errorlevel 1 (
        popd
        exit /b 1
    )
    popd
)
rem zn_task.h carries its own implementation and has a Win32 branch no suite
rem compiles, so build it here (the POSIX check-c89 does the same).
if not exist "%BUILD%\check_zn_task.c" (
    echo #define ZN_IMPLEMENTATION> "%BUILD%\check_zn_task.c"
    echo #include "zn_task.h">> "%BUILD%\check_zn_task.c"
)
cl /nologo /W4 /WX /c /I"%ROOT%" /Fo"%BUILD%\check_zn_task.obj" "%BUILD%\check_zn_task.c"
if errorlevel 1 exit /b 1
exit /b 0

:run
rem one process per test: abort() is 0xC0000409 (negative) and "if errorlevel 1"
rem compares signed, so the exit code is compared as a string.  A test can also
rem die with exit code 0 (a killed caller thread, see notes/changes.md 1.14), so it must end
rem with the harness's " OK" / " SKIP" line as well.
set "TAG=%~1"
set "FAILED=0"
set "OUT=%BUILD%\%TAG%\run.out"
pushd "%BUILD%\%TAG%"
for %%f in ("%SRC%*_test.c") do (
    echo === %TAG%/%%~nf ===
    for /f "delims=" %%t in ('%%~nf.exe --list') do (
        %%~nf.exe "@%%t" > "%OUT%" 2>&1
        set "RC=!errorlevel!"
        type "%OUT%"
        set "PASSED="
        findstr /r /c:" OK$" /c:" SKIP$" "%OUT%" >nul
        if not errorlevel 1 set "PASSED=1"
        if not "!RC!"=="0" set "PASSED="
        if not defined PASSED (
            echo *** FAILED: %TAG%/%%~nf::%%t
            set "FAILED=1"
        )
    )
)
popd
if not "!FAILED!"=="0" exit /b 1
exit /b 0
