@echo off
rem Builds the project and the tests, then runs them. The exit code is the tests'.
rem Works the same from a terminal and on GitHub Actions (.github/workflows/tests.yml)
setlocal
cd /d "%~dp0.."

rem Set up the MSVC x64 environment if cl.exe isn't already on PATH
where cl >nul 2>nul
if not errorlevel 1 goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
call "%~dp0..\build.bat"
if errorlevel 1 (echo The build failed & exit /b 1)
cd /d "%~dp0.."

if not exist build\tests mkdir build\tests

rem The tests run the editor's own code, so they link everything of it except its main()
set OBJS=build\obj\editor\miniz.obj
setlocal enabledelayedexpansion
for %%f in (src\editor\*.cpp src\engine\*.cpp src\shared\*.cpp) do if /i not "%%~nf"=="main" set OBJS=!OBJS! build\obj\editor\%%~nf.obj

echo Building tests...
cl /nologo /std:c++17 /EHsc /O1 /MD /Isrc\editor /Isrc\engine /Isrc\shared /Ivendor\raylib\include /Ivendor\raygui /Ivendor\json /Ivendor\assimp\include ^
    tests\geometry_test.cpp /Fobuild\tests\ /Febuild\tests\geometry_test.exe !OBJS! ^
    /link /LIBPATH:vendor\raylib\lib /LIBPATH:vendor\assimp\lib raylib.lib assimp.lib zlibstatic.lib winmm.lib gdi32.lib user32.lib shell32.lib opengl32.lib comdlg32.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 (echo The tests didn't build & exit /b 1)

echo Running tests...
build\tests\geometry_test.exe %*
exit /b %errorlevel%
