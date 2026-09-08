@echo off
call "D:\Coding\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"

set CMAKE="D:\Coding\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="D:\Coding\Ninja\ninja.exe"

cd /d "%~dp0"
if not exist build mkdir build
cd build

echo Configuring CMake for headless C++ music-sorter-core...
%CMAKE% -G "Ninja" -DCMAKE_MAKE_PROGRAM=%NINJA% ..\src

echo Building headless C++ music-sorter-core.exe with MSVC 2022 and Ninja...
%CMAKE% --build . --target music-sorter-core

if %ERRORLEVEL% EQU 0 (
    echo ==================================================
    echo SUCCESS: Headless C++ music-sorter-core.exe compiled!
    echo ==================================================
) else (
    echo ERROR: Build failed.
)
