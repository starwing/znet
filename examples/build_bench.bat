@if not defined vs_seted (
    call "%VS120COMNTOOLS%vsvars32.bat"
    set vs_seted=1
)

cl /nologo /Ox /MT  /Febench_flood.exe bench_flood.c
pause
