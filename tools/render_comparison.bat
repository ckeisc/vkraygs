@echo off
REM render_comparison.bat - Windows wrapper for render_comparison.py
REM
REM Usage:
REM   tools\render_comparison.bat C:\path\to\scan.spz C:\path\to\scan_camera_poses.bin C:\path\to\comparison_out
REM
REM Assumes vkgs_viewer.exe was built to build\Release\vkgs_viewer.exe
REM (run the CMake build first). Requires Python 3.8+ with numpy and Pillow:
REM   pip install numpy Pillow

setlocal
if "%~3"=="" (
    echo Usage: %~nx0 SPZ_FILE POSES_FILE OUTDIR
    echo Example: %~nx0 C:\scans\garage.spz C:\scans\garage_camera_poses.bin C:\scans\out
    exit /b 1
)

set SPZ=%~1
set POSES=%~2
set OUTDIR=%~3
set VIEWER=%~dp0..\build\Release\vkgs_viewer.exe

if not exist "%VIEWER%" (
    echo ERROR: vkgs_viewer.exe not found at %VIEWER%
    echo Build it first.
    exit /b 1
)

python "%~dp0render_comparison.py" --spz "%SPZ%" --poses "%POSES%" --viewer "%VIEWER%" --outdir "%OUTDIR%" %CULL_ARGS%
endlocal
