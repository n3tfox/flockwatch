@echo off
echo === FlockWatch Companion App Installer (Windows) ===

:: 1. Verify Python is installed and in PATH
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: Python is not installed or not added to your system PATH.
    echo Please download and install Python 3, making sure to check the box
    echo "Add python.exe to PATH" during installation.
    pause
    exit /b 1
)

set "INSTALL_DIR=%APPDATA%\flockwatch-companion"

:: 2. Create install directory
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

:: 3. Copy application files
echo Copying application files...
copy /Y "%~dp0app.py" "%INSTALL_DIR%\app.py" >nul
copy /Y "%~dp0requirements.txt" "%INSTALL_DIR%\requirements.txt" >nul

:: 4. Install requirements
echo Installing Python dependencies...
python -m pip install -r "%INSTALL_DIR%\requirements.txt"

:: 5. Create Desktop Shortcut using PowerShell
echo Creating desktop shortcut...
powershell -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%USERPROFILE%\Desktop\FlockWatch Companion.lnk'); $s.TargetPath = 'pythonw.exe'; $s.Arguments = '\"%INSTALL_DIR%\app.py\"'; $s.WorkingDirectory = '\"%INSTALL_DIR%\"'; $s.Save()"

echo === Installation Successful! ===
echo You can now launch the app using the 'FlockWatch Companion' shortcut on your Desktop.
pause
