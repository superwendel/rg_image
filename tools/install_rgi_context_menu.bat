@ECHO OFF
SetLocal EnableExtensions

SET "REPO_ROOT=%~dp0.."
FOR %%I IN ("%REPO_ROOT%") DO SET "REPO_ROOT=%%~fI"

SET "WRAPPER=%REPO_ROOT%\tools\rgi_context_convert.bat"
SET "CONVERTER=%REPO_ROOT%\rgi_convert.exe"
SET "VIEWER=%REPO_ROOT%\rgi_viewer.exe"
SET "ICON=%VIEWER%"

IF NOT EXIST "%WRAPPER%" (
    ECHO Missing wrapper:
    ECHO %WRAPPER%
    EXIT /B 1
)

IF NOT EXIST "%CONVERTER%" (
    ECHO Missing converter:
    ECHO %CONVERTER%
    ECHO Build it with: build.bat rgi_convert
    EXIT /B 1
)

IF NOT EXIST "%VIEWER%" (
    ECHO Missing viewer:
    ECHO %VIEWER%
    ECHO Build it with: build.bat rgi_viewer
    EXIT /B 1
)

CALL :add_verb ".png" "RgImageConvertToRgi" "Convert to RGI" "png-to-rgi"
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

CALL :add_verb ".rgi" "RgImageConvertToPng" "Convert to PNG" "rgi-to-png"
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

CALL :add_open_verb ".rgi"
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

ECHO Installed rg_image RGI context menu entries for the current Windows user.
ECHO If Explorer has already cached the menu, restart Explorer or sign out and back in.
EXIT /B 0

:add_verb
SET "EXTENSION=%~1"
SET "VERB=%~2"
SET "LABEL=%~3"
SET "MODE=%~4"
SET "SHELL_KEY=HKCU\Software\Classes\SystemFileAssociations\%EXTENSION%\shell\%VERB%"
SET "COMMAND=cmd.exe /d /c call ""%WRAPPER%"" %MODE% ""%%1"""

reg add "%SHELL_KEY%" /ve /d "%LABEL%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%" /v MUIVerb /d "%LABEL%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%" /v Icon /d "%ICON%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%\command" /ve /d "%COMMAND%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
EXIT /B 0

:add_open_verb
SET "EXTENSION=%~1"
SET "LABEL=Open in RGI Viewer"
SET "SHELL_KEY=HKCU\Software\Classes\SystemFileAssociations\%EXTENSION%\shell\RgImageOpenInViewer"
SET "COMMAND=""%VIEWER%"" ""%%1"""

reg add "%SHELL_KEY%" /ve /d "%LABEL%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%" /v MUIVerb /d "%LABEL%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%" /v Icon /d "%ICON%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
reg add "%SHELL_KEY%\command" /ve /d "%COMMAND%" /f >NUL
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
EXIT /B 0
