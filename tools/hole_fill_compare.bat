@echo off
REM hole_fill_compare.bat — Render hole-filling variants and compare vs Hyperscape flyby.
REM Run from the vkraygs repo root after building vkgs_viewer.exe.
REM
REM   tools\hole_fill_compare.bat C:\scans\garage.spz C:\scans\garage_camera_poses C:\scans\flyby.mp4 C:\out
REM
REM Requires: Python 3 with Pillow + numpy, ffmpeg/ffprobe on PATH.

if "%~4"=="" (
    echo Usage: %~nx0 SPZ POSES FLYBY_MP4 OUTDIR [VIEWER_EXE]
    echo Example: %~nx0 C:\scans\garage.spz C:\scans\garage_camera_poses C:\scans\flyby.mp4 C:\out
    exit /b 1
)

set SPZ=%~1
set POSES=%~2
set FLYBY=%~3
set OUTDIR=%~4
if "%~5"=="" (
    set VIEWER=build\Release\vkgs_viewer.exe
) else (
    set VIEWER=%~5
)

python tools\hole_fill_compare.py --spz "%SPZ%" --poses "%POSES%" --flyby "%FLYBY%" --viewer "%VIEWER%" --outdir "%OUTDIR%"
if errorlevel 1 (
    echo FAILED
    exit /b 1
)
echo Done. Open %OUTDIR%\comparison.html
