@if defined vs_seted goto build
@if defined VS140COMNTOOLS ( call "%VS140COMNTOOLS%vsvars32.bat" & goto vs_found)
@if defined VS120COMNTOOLS ( call "%VS120COMNTOOLS%vsvars32.bat" & goto vs_found)
@echo Can not find Visual Studio Install!
@pause
@goto eof

:vs_found
@set vs_seted=1

:build
cl /nologo /O2 /GS- /Oy /MT /Febench_flood.exe        bench_flood.c
cl /nologo /O2 /GS- /Oy /MT /Febench_echo.exe         bench_echo.c
cl /nologo /O2 /GS- /Oy /MT /Febench_flood.exe        bench_flood.c
cl /nologo /O2 /GS- /Oy /MT /Febench_echo_buffer.exe  bench_echo_buffer.c
@del *.exp *.lib *.obj
@pause
