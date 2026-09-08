@echo off
set "CARGO_HOME=D:\Coding\.cargo"
set "RUSTUP_HOME=D:\Coding\.rustup"
set "PATH=D:\Coding\.cargo\bin;%PATH%"

cd /d "%~dp0"
start "" "%~dp0music-sorter.exe"
