@echo off
setlocal
echo ===================================================
echo   VRM2MDL Dependency Checker for Windows 11
echo ===================================================
echo.

set MISSING=0

:: 1. CMake
echo [*] Checking for CMake...
cmake --version >nul 2>&1
if errorlevel 1 (
    echo [!] CMake is NOT found.
    set MISSING=1
) else (
    echo [OK] CMake found.
)

:: 2. MSVC (cl.exe)
echo [*] Checking for MSVC Compiler (cl.exe)...
cl.exe >nul 2>&1
if not errorlevel 1 (
    echo [OK] MSVC Compiler is ready.
    goto :CHECK_RUST
)

echo [!] MSVC Compiler is NOT found in PATH.
echo [*] Attempting to locate Visual Studio 2022...

:: Avoid parentheses in IF/FOR by using a helper variable
set "VSPATH86=%ProgramFiles(x86)%"
set "VSW_EXE=%VSPATH86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSW_EXE%" (
    echo [!] Visual Studio Installer not found.
    set MISSING=1
    goto :CHECK_RUST
)

for /f "usebackq tokens=*" %%i in (`"%VSW_EXE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if "%VS_PATH%"=="" (
    echo [!] Could not find a valid VS 2022 installation.
    set MISSING=1
) else (
    echo [OK] Found VS 2022 at: "%VS_PATH%"
    echo [TIP] Please run: call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
)

:CHECK_RUST
:: 3. Rust
echo [*] Checking for Rust (cargo)...
cargo --version >nul 2>&1
if errorlevel 1 (
    echo [!] Rust/Cargo is NOT found.
    set MISSING=1
) else (
    echo [OK] Rust found.
)

:: 4. Git
echo [*] Checking for Git...
git --version >nul 2>&1
if errorlevel 1 (
    echo [!] Git is NOT found.
    set MISSING=1
) else (
    echo [OK] Git found.
)

echo.
echo ---------------------------------------------------
if "%MISSING%"=="0" (
    echo [SUCCESS] All required packages are installed!
    echo You can now build the project using build.bat
) else (
    echo [FAILURE] Some packages are missing.
)
echo ---------------------------------------------------
pause
