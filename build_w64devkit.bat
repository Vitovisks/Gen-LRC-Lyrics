@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo   Gen LRC Lyrics - compilacao para WACUP
echo ==========================================
echo.

where g++ >nul 2>nul
if errorlevel 1 (
    echo ERRO: g++ nao foi encontrado.
    echo Abra este arquivo pelo terminal do w64devkit.
    echo.
    pause
    exit /b 1
)

for /f %%i in ('g++ -dumpmachine') do set "TRIPLE=%%i"
echo Compilador: %TRIPLE%
echo.

echo %TRIPLE% | findstr /i "i686" >nul
if not errorlevel 1 goto :compile_native_x86

echo O compilador nao parece ser i686.
echo Tentando compilar para 32 bits usando -m32...
echo.
g++ -m32 -std=c++17 -O2 -s -shared -static -static-libgcc -static-libstdc++ -o "gen_lrclyrics.dll" "gen_lrclyrics.cpp" -lgdi32 -luser32
if errorlevel 1 goto :failed

goto :success

:compile_native_x86
echo Compilando com a toolchain i686...
g++ -std=c++17 -O2 -s -shared -static -static-libgcc -static-libstdc++ -o "gen_lrclyrics.dll" "gen_lrclyrics.cpp" -lgdi32 -luser32
if errorlevel 1 goto :failed
goto :success

:failed
echo.
echo A compilacao falhou.
echo Copie e mande aqui TODA a mensagem de erro acima.
echo.
pause
exit /b 1

:success
echo.
if exist "gen_lrclyrics.dll" (
    echo SUCESSO! DLL criada:
    echo %CD%\gen_lrclyrics.dll
) else (
    echo O compilador terminou, mas a DLL nao foi encontrada.
)
echo.
pause
endlocal
