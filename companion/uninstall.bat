@echo off
echo === FlockWatch Companion App Uninstaller (Windows) ===

set "INSTALL_DIR=%APPDATA%\flockwatch-companion"

echo Removing application files...
if exist "%INSTALL_DIR%" rmdir /S /Q "%INSTALL_DIR%"

echo Removing desktop shortcut...
if exist "%USERPROFILE%\Desktop\FlockWatch Companion.lnk" del /F /Q "%USERPROFILE%\Desktop\FlockWatch Companion.lnk"

echo === Uninstallation Complete! ===
pause
