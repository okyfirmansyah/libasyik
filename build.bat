@echo on
REM Setup MSVC environment (needed for Windows SDK headers/libs)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Add LLVM, Ninja, CMake, and PowerShell 7 to PATH
set PATH=C:\Program Files\LLVM\bin;%PATH%
set PATH=C:\Users\okyfi\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%
set PATH=C:\Program Files\CMake\bin;%PATH%
set PATH=C:\Program Files\PowerShell\7;%PATH%

REM Set vcpkg root
set VCPKG_ROOT=C:\vcpkg

REM Tell vcpkg to use system-installed tools (ninja, pwsh, etc.) instead of downloading.
set VCPKG_FORCE_SYSTEM_BINARIES=1

REM Workaround for vcpkg curl SSL error 35 (CRYPT_E_REVOCATION_OFFLINE):
REM vcpkg's internal curl fails Schannel certificate revocation checks against
REM GitHub's CDN. Use x-script asset source to download via system curl with
REM --ssl-no-revoke flag instead.
set X_VCPKG_ASSET_SOURCES=clear;x-script,curl --ssl-no-revoke -L {url} --output {dst}

REM Clean previous build if needed
if "%1"=="clean" (
    rd /s /q build 2>nul
    mkdir build 2>nul
)

REM Configure
echo.
echo === CMake Configure ===
cmake --preset windows-clang-cl -DCMAKE_MAKE_PROGRAM=ninja
if errorlevel 1 (
    echo CMake configure failed!
    exit /b 1
)

REM Build
echo.
echo === CMake Build ===
cmake --build build
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo.
echo === Build Successful ===
