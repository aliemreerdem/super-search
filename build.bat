@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo  Compiling SuperSearch (High-Performance Parallel Engine)
echo ========================================================

if not exist bin mkdir bin

where gcc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    set "CC=gcc"
    set "CFLAGS=-O3 -flto=auto -Iinclude -Wall -Wextra -Wno-unused-parameter"
    set "CFLAGS_FALLBACK=-O3 -Iinclude -Wall -Wextra -Wno-unused-parameter"
) else (
    where clang >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        set "CC=clang"
        set "CFLAGS=-O3 -flto -Iinclude -Wall -Wextra -Wno-unused-parameter"
        set "CFLAGS_FALLBACK=-O3 -Iinclude -Wall -Wextra -Wno-unused-parameter"
    ) else (
        echo [ERROR] Neither 'gcc' nor 'clang' was found in PATH.
        echo Please install MinGW-w64 GCC or LLVM/Clang and try again.
        exit /b 1
    )
)

set SRC=src\main.c src\utils.c src\scanner.c src\engine.c src\threadpool.c src\report.c src\ignore.c src\replace.c src\regex_engine.c src\tinf.c src\archive.c src\mcp_server.c src\interactive.c src\semantic.c src\cache.c
set OUT=bin\supersearch.exe

echo Compiler : %CC%
echo Flags    : %CFLAGS%
echo Sources  : %SRC%
echo Output   : %OUT%
echo.

"%CC%" %CFLAGS% %SRC% -o %OUT%
if %ERRORLEVEL% NEQ 0 (
    echo [INFO] LTO link failed on this toolchain; retrying with standard -O3 optimization...
    "%CC%" %CFLAGS_FALLBACK% %SRC% -o %OUT%
)

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] SuperSearch built successfully! -^> %OUT%
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
