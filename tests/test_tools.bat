@ECHO OFF
SETLOCAL EnableExtensions
CD /D "%~dp0.."

IF NOT EXIST build MKDIR build
tool_fixture.exe generate || EXIT /B 1
rgi_convert.exe build\tool_current.rgi build\tool.png || EXIT /B 1
tool_fixture.exe corrupt-png build\tool.png build\tool_bad_crc.png build\tool_bad_adler.png || EXIT /B 1
rgi_convert.exe build\tool_bad_crc.png build\bad_crc.rgi >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1
rgi_convert.exe build\tool_bad_adler.png build\bad_adler.rgi >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1

rgi_convert.exe build\tool.png build\tool.qoi || EXIT /B 1
rgi_convert.exe build\tool.qoi build\tool_roundtrip.rgi || EXIT /B 1
FC /B build\tool_current.rgi build\tool_roundtrip.rgi >NUL || EXIT /B 1

IF EXIST build\tool_tree (
    ECHO Refusing to replace pre-existing test tree: build\tool_tree
    EXIT /B 1
)
MKDIR build\tool_tree\input\nested || EXIT /B 1
MKDIR build\tool_tree\linked_source || EXIT /B 1
COPY /Y build\tool.png build\tool_tree\input\nested\sample.png >NUL || EXIT /B 1
COPY /Y build\tool.png build\tool_tree\linked_source\linked.png >NUL || EXIT /B 1
FOR %%I IN ("build\tool_tree\linked_source") DO SET "LINK_TARGET=%%~fI"
MKLINK /J build\tool_tree\input\linked "%LINK_TARGET%" >NUL || EXIT /B 1
FOR %%I IN ("build\tool_tree\input") DO SET "INPUT_TARGET=%%~fI"
MKLINK /J build\tool_tree\output_alias "%INPUT_TARGET%" >NUL || EXIT /B 1

rgi_convert.exe --dir build\tool_tree\input build\tool_tree\input_sibling --rgi --recursive || EXIT /B 1
IF NOT EXIST build\tool_tree\input_sibling\nested\sample.rgi EXIT /B 1
IF EXIST build\tool_tree\input_sibling\linked\linked.rgi EXIT /B 1

rgi_convert.exe --dir build\tool_tree\input build\tool_tree\input --rgi --recursive >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1
rgi_convert.exe --dir build\tool_tree\input build\tool_tree\input\generated --rgi --recursive >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1
rgi_convert.exe --dir build\tool_tree\input build\tool_tree\output_alias --rgi --recursive >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1

RMDIR build\tool_tree\input\linked || EXIT /B 1
RMDIR build\tool_tree\output_alias || EXIT /B 1
RMDIR /S /Q build\tool_tree || EXIT /B 1

DIR /B build\*.tmp.* >NUL 2>NUL
IF NOT ERRORLEVEL 1 EXIT /B 1

ECHO RGI tool tests passed.
EXIT /B 0
