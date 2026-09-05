@echo off
call "D:\Coding\VSBuildTools\VC\Auxiliary\Build\vcvars64.bat"

set CMAKE="D:\Coding\VSBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="D:\Coding\Ninja\ninja.exe"

cd /d "%~dp0"
if not exist build mkdir build
cd build

echo Configuring CMake for native C++ MusicSorterTests...
%CMAKE% -G "Ninja" -DCMAKE_MAKE_PROGRAM=%NINJA% ..\src

echo Building MusicSorterTests with MSVC 2022 and Ninja...
%CMAKE% --build . --target MusicSorterTests

if %ERRORLEVEL% EQU 0 (
    echo ==================================================
    echo Starting memory watchdog and running MusicSorterTests.exe...
    echo ==================================================
    cd ..
    start /b powershell -NoProfile -Command "while ($true) { Start-Sleep -Seconds 30; $p = Get-Process MusicSorterTests -ErrorAction SilentlyContinue; if (!$p) { break }; if ($p.WorkingSet64 -gt 15GB) { Write-Host '[WATCHDOG ALERT] MusicSorterTests exceeded 15GB RAM! Terminating immediately...'; Stop-Process -Id $p.Id -Force; break } }"
    .\MusicSorterTests.exe
) else (
    echo ERROR: Build failed.
)
