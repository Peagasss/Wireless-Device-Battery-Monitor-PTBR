@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo [1/3] Verificando compilador MSVC...

where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 goto COMPILE

:: Localizar vswhere ou vcvarsall.bat
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_DIR=%%i"
    )
)

if defined VS_DIR (
    if exist "!VS_DIR!\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=!VS_DIR!\VC\Auxiliary\Build\vcvarsall.bat"
        goto FOUND_VCVARS
    )
)

:: Procura manual nos caminhos padrão do Visual Studio
for %%v in (18 2022 2019) do (
    for %%e in (Community Professional Enterprise BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat"
            goto FOUND_VCVARS
        )
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvarsall.bat"
            goto FOUND_VCVARS
        )
    )
)

echo ERRO: Visual Studio com suporte C++ x64 nao foi encontrado.
echo Certifique-se de que o Visual Studio com o workload 'Desenvolvimento para Desktop com C++' esteja instalado.
exit /b 1

:FOUND_VCVARS
echo Inicializando ambiente MSVC x64 via: "%VCVARS%"
call "%VCVARS%" x64 >nul || exit /b 1

:COMPILE
if not exist build_msvc mkdir build_msvc
del /q build_msvc\*.obj >nul 2>&1

echo [2/3] Compilando recursos...
rc.exe /nologo /fo build_msvc\app.res res\app.rc || exit /b 1

echo [3/3] Compilando e linkando WirelessBattery.exe...
set SOURCES=
for %%f in (src\*.cpp src\drivers\*.cpp) do set SOURCES=!SOURCES! %%f

cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /D_UNICODE /DUNICODE /W3 ^
    /c !SOURCES! /Fobuild_msvc\ || exit /b 1

link.exe /nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
    build_msvc\*.obj build_msvc\app.res ^
    hid.lib setupapi.lib cfgmgr32.lib ole32.lib oleaut32.lib comctl32.lib gdi32.lib ^
    runtimeobject.lib ws2_32.lib user32.lib shell32.lib advapi32.lib ^
    /OUT:WirelessBattery.exe || exit /b 1

echo.
echo ========================================================
echo Sucesso! WirelessBattery.exe compilado com MSVC (x64).
echo ========================================================
