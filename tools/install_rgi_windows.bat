@ECHO OFF
SetLocal EnableExtensions

SET "REPO_ROOT=%~dp0.."
FOR %%I IN ("%REPO_ROOT%") DO SET "REPO_ROOT=%%~fI"

PUSHD "%REPO_ROOT%" >NUL
CALL build.bat rgi_viewer
IF %ERRORLEVEL% NEQ 0 (
    POPD >NUL
    EXIT /B %ERRORLEVEL%
)

CALL build.bat rgi_thumbnail
IF %ERRORLEVEL% NEQ 0 (
    POPD >NUL
    EXIT /B %ERRORLEVEL%
)

"%REPO_ROOT%\rgi_viewer.exe" --register-all
SET "REGISTER_RESULT=%ERRORLEVEL%"
POPD >NUL

IF NOT "%REGISTER_RESULT%"=="0" EXIT /B %REGISTER_RESULT%

ECHO Installed RGI Windows file associations and Explorer thumbnails for the current user.
ECHO If Explorer has cached old metadata, restart Explorer or sign out and back in.
EXIT /B 0
