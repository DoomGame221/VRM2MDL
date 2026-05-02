@echo off
setlocal
echo ===================================================
echo   VRM2MDL Auto Build Script (Windows 11)
echo ===================================================
echo.

:: 1. Locate VS 2022
set "VSPATH86=%ProgramFiles(x86)%"
set "VSW_EXE=%VSPATH86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSW_EXE%" (
    echo [ERROR] Visual Studio Installer not found.
    pause
    exit /b 1
)

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%VSW_EXE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if "%VS_PATH%"=="" (
    echo [ERROR] Visual Studio 2022 with C++ Tools not found.
    pause
    exit /b 1
)

:: 2. Initialize MSVC Environment
echo [*] Initializing MSVC 64-bit environment
set "VCVARS=%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [ERROR] Could not find vcvars64.bat
    pause
    exit /b 1
)

call "%VCVARS%" >nul

:: Add CMake to PATH if not present
cmake --version >nul 2>&1
if not errorlevel 1 goto :CMAKE_OK
if exist "C:\Program Files\CMake\bin" set "PATH=%PATH%;C:\Program Files\CMake\bin"
if exist "C:\Program Files (x86)\CMake\bin" set "PATH=%PATH%;C:\Program Files (x86)\CMake\bin"

:CMAKE_OK
:: 3. Run CMake
echo [*] Configuring Project with CMake
if not exist "build" mkdir build
:: Explicitly passing Assimp path to help CMake find it in non-standard locations
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASSIMP_ROOT="C:/Program Files/Assimp"

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

:: 4. Build C++
echo [*] Building Project Release
cmake --build build --config Release

if errorlevel 1 (
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

:: 5. Build Rust GUI
echo [*] Checking for Rust cargo
cargo --version >nul 2>&1
if errorlevel 1 (
    echo [WARN] Rust/Cargo not found. Skipping GUI build.
    goto :ORGANIZE
)

echo [*] Building Rust GUI Release
cd vrm_gui
cargo build --release
cd ..

:ORGANIZE
:: 6. Organize Release Files
echo [*] Organizing release files
if not exist "release" mkdir release
if not exist "release\cores" mkdir release\cores

:: Copy Modular Cores
if exist "build\bin\Release\vrm_core.dll" (
    copy /y "build\bin\Release\vrm_core.dll" "release\cores\"
)
if exist "build\bin\Release\assimp_core.dll" (
    copy /y "build\bin\Release\assimp_core.dll" "release\cores\"
)

:: Copy Rust GUI
if exist "vrm_gui\target\release\vrm_gui.exe" (
    copy /y "vrm_gui\target\release\vrm_gui.exe" "release\"
)

echo.
echo ===================================================
echo [SUCCESS] Build Complete
echo All files are ready in the release folder:
echo   - release\vrm_gui.exe (GUI)
echo   - release\cores\vrm_core.dll (VRM Core)
echo   - release\cores\assimp_core.dll (FBX/OBJ Core)
echo ===================================================
pause

