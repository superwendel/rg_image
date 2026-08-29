@ECHO OFF
SetLocal EnableExtensions

SET "LOG=%TEMP%\rg-image-rgi-context.log"
>>"%LOG%" ECHO [%DATE% %TIME%] %~nx0 %*

IF "%~2"=="" GOTO :usage

SET "MODE=%~1"
SET "INPUT=%~2"
SET "REPO_ROOT=%~dp0.."
FOR %%I IN ("%REPO_ROOT%") DO SET "REPO_ROOT=%%~fI"
FOR %%I IN ("%INPUT%") DO SET "INPUT_FULL=%%~fI"
>>"%LOG%" ECHO   mode=%MODE%
>>"%LOG%" ECHO   input=%INPUT_FULL%

IF NOT EXIST "%INPUT_FULL%" GOTO :input_missing

SET "CONVERTER=%REPO_ROOT%\rgi_convert.exe"
IF /I "%MODE%"=="png-to-rgi" GOTO :png_to_rgi
IF /I "%MODE%"=="rgi-to-png" GOTO :rgi_to_png

ECHO Unknown conversion mode: %MODE%
>>"%LOG%" ECHO   error=unknown conversion mode
GOTO :usage

:png_to_rgi
IF /I NOT "%~x2"==".png" GOTO :expected_png
SET "OUTPUT=%~dpn2.rgi"
GOTO :convert

:rgi_to_png
IF /I "%~x2"==".rgi" GOTO :rgi_extension_ok
GOTO :expected_rgi

:rgi_extension_ok
SET "OUTPUT=%~dpn2.png"
GOTO :convert

:convert
>>"%LOG%" ECHO   output=%OUTPUT%
>>"%LOG%" ECHO   converter=%CONVERTER%

IF NOT EXIST "%CONVERTER%" GOTO :missing_converter

PUSHD "%REPO_ROOT%" || GOTO :fail_pause
"%CONVERTER%" "%INPUT_FULL%" "%OUTPUT%"
SET "RESULT=%ERRORLEVEL%"
POPD

IF NOT "%RESULT%"=="0" (
    ECHO Conversion failed with exit code %RESULT%.
    >>"%LOG%" ECHO   exit=%RESULT%
    GOTO :fail_pause
)

>>"%LOG%" ECHO   exit=0
EXIT /B 0

:usage
ECHO Usage: %~nx0 png-to-rgi input.png
ECHO        %~nx0 rgi-to-png input.rgi
GOTO :fail_pause

:input_missing
ECHO Input file does not exist:
ECHO %INPUT_FULL%
>>"%LOG%" ECHO   error=input file does not exist
GOTO :fail_pause

:missing_converter
ECHO Missing converter:
ECHO %CONVERTER%
ECHO Build it with: build.bat rgi_convert
>>"%LOG%" ECHO   error=missing converter
GOTO :fail_pause

:expected_png
ECHO Expected a .png file:
ECHO %INPUT_FULL%
GOTO :fail_pause

:expected_rgi
ECHO Expected a .rgi file:
ECHO %INPUT_FULL%
GOTO :fail_pause

:fail_pause
ECHO.
PAUSE
EXIT /B 1
