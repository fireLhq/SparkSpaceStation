@echo off
setlocal
pushd "%~dp0" || exit /b 1
if not defined MINGW_BIN set "MINGW_BIN=D:\msys64\mingw64\bin"
if not defined EASYX_ROOT set "EASYX_ROOT=D:\EasyX"
set "FLAGS=-Wall -Wextra -Werror -finput-charset=GBK -fexec-charset=GBK"
if not exist "%MINGW_BIN%\gcc.exe" (
    echo Set MINGW_BIN to the 64-bit MinGW-w64 bin directory.
    goto fail
)
if not exist "%EASYX_ROOT%\lib64\libeasyx.a" (
    echo Set EASYX_ROOT to the MinGW EasyX directory with include and lib64.
    goto fail
)

"%MINGW_BIN%\gcc.exe" -std=c11 %FLAGS% server\server.c -o server\hospital_server.exe -lws2_32 -lbcrypt -ladvapi32 -luser32
if errorlevel 1 goto fail
"%MINGW_BIN%\gcc.exe" -std=c11 %FLAGS% clients\console\console_client.c -o clients\console\hospital_console.exe -lws2_32 -luser32
if errorlevel 1 goto fail
"%MINGW_BIN%\g++.exe" -std=c++11 %FLAGS% -Wno-unknown-pragmas -static -I"%EASYX_ROOT%\include" clients\easyx\easyx_client.cpp -L"%EASYX_ROOT%\lib64" -o clients\easyx\hospital_easyx.exe -leasyx -lws2_32 -luser32 -lgdi32 -lwinmm -lole32
if errorlevel 1 goto fail
"%MINGW_BIN%\gcc.exe" -std=c11 %FLAGS% tools\data_tool.c -o tools\data_tool.exe -lbcrypt -ladvapi32
if errorlevel 1 goto fail
echo Server, both clients, and data tool built.
popd
exit /b 0

:fail
echo Build failed.
popd
exit /b 1
