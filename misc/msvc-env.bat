@echo off
rem misc/msvc-env.bat -- put cl.exe on PATH when the MSVC environment is not
rem loaded yet (a plain ssh or cmd session, not a Developer Command Prompt).
rem
rem Call it so the environment survives in the caller:
rem
rem   call "%~dp0..\misc\msvc-env.bat"
rem   if errorlevel 1 echo no MSVC C++ toolset found
rem
rem Returns 0 when cl is usable afterwards, 1 when no toolset was found.
rem vswhere ships with every VS 2017+ installer; VS140COMNTOOLS covers the
rem VS2015 layout.
setlocal

where cl >nul 2>&1
if not errorlevel 1 (
    endlocal
    exit /b 0
)

rem escape hatch when vswhere cannot find the toolset:
rem   set ZNET_VCVARS=C:\path\to\vcvars64.bat
if defined ZNET_VCVARS if exist "%ZNET_VCVARS%" (
    endlocal & call "%ZNET_VCVARS%" >nul
    goto :verify
)

set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=%ProgramFiles%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDIR=%%i"
if not defined VSDIR if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do set "VSDIR=%%i"
if not defined VSDIR if defined VS140COMNTOOLS for %%i in ("%VS140COMNTOOLS%..\..") do set "VSDIR=%%~fi"

if defined VSDIR if exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    endlocal & call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
    goto :verify
)
if defined VSDIR if exist "%VSDIR%\VC\vcvarsall.bat" (
    endlocal & call "%VSDIR%\VC\vcvarsall.bat" x64 >nul
    goto :verify
)
if defined VSDIR if exist "%VSDIR%\Common7\Tools\VsDevCmd.bat" (
    endlocal & call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=amd64 -host_arch=amd64 >nul
    goto :verify
)

endlocal
exit /b 1

:verify
where cl >nul 2>&1
if errorlevel 1 exit /b 1
exit /b 0
