@echo off
call "D:\Coding\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0"
echo Compiling native C++ MusicSorterApp.exe with MSVC cl.exe...

cl.exe /std:c++20 /O2 /W3 /EHsc /MD main.cpp /Fe:..\MusicSorterApp.exe /link user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib winmm.lib

if %ERRORLEVEL% EQU 0 (
    echo SUCCESS: Native C++ MusicSorterApp.exe compiled successfully!
) else (
    echo ERROR: Compilation failed.
)
