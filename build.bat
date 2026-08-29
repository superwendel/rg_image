@ECHO OFF
SETLOCAL EnableExtensions
CD /D "%~dp0"

IF NOT DEFINED RG_CORE_DIR SET "RG_CORE_DIR=%~dp0..\rg_core"
FOR %%I IN ("%RG_CORE_DIR%") DO SET "RG_CORE_DIR=%%~fI"
IF NOT EXIST "%RG_CORE_DIR%\src\rg_defs.h" (
    ECHO rg_core not found at "%RG_CORE_DIR%".
    ECHO Set RG_CORE_DIR to the rg_core repository root.
    EXIT /B 1
)

SET "BASE_FLAGS=/nologo /W4 /WX /D_CRT_SECURE_NO_WARNINGS /std:c11 /I src /I "%RG_CORE_DIR%\src""
SET "OPT_FLAGS=/O2"

IF "%~1"=="" GOTO help
IF /I "%~1"=="test" GOTO test
IF /I "%~1"=="test_rgi" GOTO test_rgi
IF /I "%~1"=="test_scalar" GOTO test_scalar
IF /I "%~1"=="test_cpp" GOTO test_cpp
IF /I "%~1"=="test_allocator" GOTO test_allocator
IF /I "%~1"=="test_tools" GOTO test_tools
IF /I "%~1"=="example" GOTO example
IF /I "%~1"=="rgi_convert" GOTO rgi_convert
IF /I "%~1"=="rgi_migrate" GOTO rgi_migrate
IF /I "%~1"=="rgi_viewer" GOTO rgi_viewer
IF /I "%~1"=="rgi_thumbnail" GOTO rgi_thumbnail
IF /I "%~1"=="tools" GOTO tools
IF /I "%~1"=="bench" GOTO bench
GOTO help_error

:test
CALL "%~f0" test_rgi || EXIT /B 1
CALL "%~f0" test_scalar || EXIT /B 1
CALL "%~f0" test_cpp || EXIT /B 1
CALL "%~f0" test_allocator || EXIT /B 1
CALL "%~f0" example || EXIT /B 1
CALL "%~f0" test_tools || EXIT /B 1
ECHO All rg_image tests passed.
EXIT /B 0

:test_rgi
cl %BASE_FLAGS% %OPT_FLAGS% tests\test_rgi.c /Fe:rg_rgi_test.exe
IF ERRORLEVEL 1 EXIT /B 1
rg_rgi_test.exe
EXIT /B %ERRORLEVEL%

:test_scalar
cl %BASE_FLAGS% %OPT_FLAGS% /DRG_RGI_NO_SIMD tests\test_rgi.c /Fe:rg_rgi_test_scalar.exe
IF ERRORLEVEL 1 EXIT /B 1
rg_rgi_test_scalar.exe
EXIT /B %ERRORLEVEL%

:test_cpp
cl /nologo /W4 /WX /EHsc /std:c++17 /I src /I "%RG_CORE_DIR%\src" tests\test_rgi_cpp.cpp /Fe:rg_rgi_test_cpp.exe
IF ERRORLEVEL 1 EXIT /B 1
rg_rgi_test_cpp.exe
EXIT /B %ERRORLEVEL%

:test_allocator
cl %BASE_FLAGS% %OPT_FLAGS% tests\test_rgi_allocator.c /Fe:rg_rgi_test_allocator.exe
IF ERRORLEVEL 1 EXIT /B 1
rg_rgi_test_allocator.exe
EXIT /B %ERRORLEVEL%

:example
cl %BASE_FLAGS% %OPT_FLAGS% examples\example_rgi.c /Fe:example_rgi.exe
IF ERRORLEVEL 1 EXIT /B 1
example_rgi.exe
EXIT /B %ERRORLEVEL%

:rgi_convert
cl %BASE_FLAGS% %OPT_FLAGS% tools\rgi_convert.c third_party\miniz\miniz_tinfl.c /Fe:rgi_convert.exe
IF ERRORLEVEL 1 EXIT /B 1
ECHO Built rgi_convert.exe
EXIT /B 0

:rgi_migrate
cl %BASE_FLAGS% %OPT_FLAGS% tools\rgi_migrate.c /Fe:rgi_migrate.exe
IF ERRORLEVEL 1 EXIT /B 1
ECHO Built rgi_migrate.exe
EXIT /B 0

:test_tools
CALL "%~f0" rgi_convert || EXIT /B 1
CALL "%~f0" rgi_migrate || EXIT /B 1
cl %BASE_FLAGS% %OPT_FLAGS% tests\tool_fixture.c /Fe:tool_fixture.exe
IF ERRORLEVEL 1 EXIT /B 1
CALL tests\test_tools.bat
EXIT /B %ERRORLEVEL%

:tools
CALL "%~f0" rgi_convert || EXIT /B 1
CALL "%~f0" rgi_migrate || EXIT /B 1
EXIT /B 0

:rgi_viewer
IF NOT DEFINED SDL3_DIR FOR /D %%I IN ("C:\libs\SDL3-*") DO IF NOT DEFINED SDL3_DIR SET "SDL3_DIR=%%I"
IF NOT DEFINED SDL3_DIR (
    ECHO SDL3 not found. Set SDL3_DIR to an SDL3 development package.
    EXIT /B 1
)
IF NOT EXIST "%SDL3_DIR%\include\SDL3\SDL.h" (
    ECHO SDL3 headers not found under "%SDL3_DIR%".
    EXIT /B 1
)
IF NOT DEFINED TARGET_ARCH SET "TARGET_ARCH=x64"
cl %BASE_FLAGS% %OPT_FLAGS% tools\rgi_viewer.c /Fe:rgi_viewer.exe ^
    /I "%SDL3_DIR%\include" ^
    /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /LIBPATH:"%SDL3_DIR%\lib\%TARGET_ARCH%" SDL3.lib Shell32.lib Advapi32.lib
IF ERRORLEVEL 1 EXIT /B 1
ECHO Built rgi_viewer.exe
EXIT /B 0

:rgi_thumbnail
cl %BASE_FLAGS% %OPT_FLAGS% /LD tools\rgi_thumbnail_provider.c /Fe:rgi_thumbnail.dll ^
    /link /DEF:tools\rgi_thumbnail_provider.def Ole32.lib Gdi32.lib Uuid.lib
IF ERRORLEVEL 1 EXIT /B 1
ECHO Built rgi_thumbnail.dll
EXIT /B 0

:bench
cl %BASE_FLAGS% %OPT_FLAGS% tests\bench_rgi.c /Fe:bench_rgi.exe
IF ERRORLEVEL 1 EXIT /B 1
bench_rgi.exe
EXIT /B %ERRORLEVEL%

:help
ECHO Usage: build.bat ^<target^>
ECHO.
ECHO Tests: test, test_rgi, test_scalar, test_cpp, test_allocator, test_tools
ECHO Tools: rgi_convert, rgi_migrate, rgi_viewer, rgi_thumbnail, tools
ECHO Other: example, bench
EXIT /B 0

:help_error
ECHO Unknown target: %~1
GOTO help
