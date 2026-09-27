@echo off
rem Builds rename_movies.exe with MSVC. Requires the Visual Studio Build Tools
rem (C++ workload) - vcvars64.bat sets up the compiler environment.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /O1 /W3 /MT rename_movies.c /Fe:rename_movies.exe /link /SUBSYSTEM:CONSOLE
del /q rename_movies.obj >nul 2>&1
