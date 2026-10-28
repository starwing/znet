@echo off
rem bench\build.bat -- build every benchmark program with MSVC.
rem
rem Works from a plain ssh/cmd session too: misc\msvc-env.bat locates
rem Visual Studio and loads the MSVC environment when cl.exe is missing.
setlocal

set "SRC=%~dp0"
set "ROOT=%SRC%.."
set "BUILD=%SRC%build\msvc"

call "%~dp0..\misc\msvc-env.bat"
if errorlevel 1 (
    echo build.bat: cl.exe not found; install the MSVC C++ build tools.
    exit /b 2
)
if not exist "%BUILD%" mkdir "%BUILD%"

pushd "%BUILD%"
for %%f in ("%SRC%*.c") do (
    echo --- %%~nxf ---
    cl /nologo /O2 /W4 /WX /I"%ROOT%" /I"%SRC%/." "%%~ff"
    if errorlevel 1 (
        popd
        exit /b 1
    )
)
popd

echo built:
dir /b "%BUILD%\*.exe"
exit /b 0
