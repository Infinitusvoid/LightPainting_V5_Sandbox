@echo off
REM ================================
REM Make new W_<date>_<time>.h file
REM and update main.cpp
REM ================================

REM Call the Python script in the same folder.
REM Requires "python" to be available on PATH.
python "%~dp0make_new_version.py"

if errorlevel 1 (
    echo.
    echo [ERROR] make_new_version.py failed.
) else (
    echo.
    echo Done. New version header created and main.cpp updated.
)

pause
