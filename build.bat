@echo off

REM Usage: build.bat
REM build.bat             build everything
REM build.bat dll         build symphytum.dll only

setlocal enabledelayedexpansion
cd /d %~dp0

set ZIG=zig
if not exist build mkdir build

set INC=-Iinclude -Igenerated
set DEF=-DUNICODE -D_UNICODE
set WARN=-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wdouble-promotion -Wformat=2 -Wcast-align -Wnull-dereference -Wswitch-enum
set OPT=-target x86_64-windows-gnu -O3 -std=c++23 %WARN% %INC% %DEF%

set WHAT=%1
if "%WHAT%"=="" set WHAT=all

if /i "%WHAT%"=="dll"    (call :build_dll    & goto :done)
if /i "%WHAT%"=="all" (
    call :build_dll
    goto :done
)
exit /b 2

:build_dll
set SRC=^
src\dllmain.cpp ^
src\init.cpp ^
src\log.cpp ^
src\config.cpp ^
src\hook.cpp ^
src\il2cpp\il2cpp_names.cpp ^
src\il2cpp\il2cpp.cpp ^
src\patches\request.cpp ^
src\patches\ssl.cpp ^
src\patches\crypto.cpp
%ZIG% c++ %OPT% -shared %SRC% -lkernel32 -luser32 -lole32 -ladvapi32 -o build\symphytum.dll
if errorlevel 1 ( echo DLL BUILD FAILED & exit /b 1 )
echo built build\symphytum.dll
exit /b 0

:done
endlocal
