@echo off
REM Scope-clock host preview: rasterize every scene and render scope-style
REM PNG/SVG into tools\out\.  Extra args pass through to preview.py, e.g.:
REM   run.cmd --now
REM   run.cmd --time 13:37:00 --message "HELLO WORLD" --show-retrace
setlocal
set PY=%~dp0.venv\Scripts\python.exe
if not exist "%PY%" (
  echo [scope-clock] project venv not found.
  echo   python -m venv .venv
  echo   .venv\Scripts\python -m pip install -r tools\requirements.txt
  pause
  exit /b 1
)
"%PY%" "%~dp0tools\preview.py" %*
if errorlevel 1 pause
