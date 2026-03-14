@echo off
REM Setup MSVC environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Add vcpkg DLLs to PATH so test exe can find them
set PATH=C:\Users\okyfi\libasyik\build\vcpkg_installed\x64-windows\debug\bin;%PATH%
set PATH=C:\Users\okyfi\libasyik\build\vcpkg_installed\x64-windows\bin;%PATH%

cd /d C:\Users\okyfi\libasyik\build\tests

echo === Running All Tests (excluding SQL) ===
C:\Users\okyfi\libasyik\build\tests\libasyik_test.exe ~[sql] --reporter compact 2>&1
echo.
echo === Test Exit Code: %errorlevel% ===
