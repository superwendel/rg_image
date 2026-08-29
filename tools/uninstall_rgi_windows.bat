@ECHO OFF
SetLocal EnableExtensions

SET "REPO_ROOT=%~dp0.."
FOR %%I IN ("%REPO_ROOT%") DO SET "REPO_ROOT=%%~fI"

IF NOT EXIST "%REPO_ROOT%\rgi_viewer.exe" (
    ECHO Missing rgi_viewer.exe. Build it with: build.bat rgi_viewer
    EXIT /B 1
)

"%REPO_ROOT%\rgi_viewer.exe" --unregister-all
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

ECHO Removed RGI Windows file associations and Explorer thumbnail registrations for the current user.
ECHO If Explorer has cached old metadata, restart Explorer or sign out and back in.
EXIT /B 0
