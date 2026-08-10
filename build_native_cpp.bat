@echo off
call "D:\Coding\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"

set CMAKE="D:\Coding\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="D:\Coding\Ninja\ninja.exe"

cd /d "%~dp0"
if not exist build mkdir build
cd build

echo Configuring CMake for native C++ MusicSorterApp...
%CMAKE% -G "Ninja" -DCMAKE_MAKE_PROGRAM=%NINJA% ..\src

echo Building native C++ MusicSorterApp.exe with MSVC 2022 and Ninja...
%CMAKE% --build .

if %ERRORLEVEL% EQU 0 (
    echo ==================================================
    echo SUCCESS: Native C++ MusicSorterApp.exe compiled!
    echo ==================================================
) else (
    echo ERROR: Build failed.
)
