@echo off
setlocal
cd /d "%~dp0"

rem Set up the MSVC x64 environment if cl.exe isn't already on PATH
where cl >nul 2>nul
if not errorlevel 1 goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%i"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
rem The game and the editor both build src\shared, so each gets its own folder of .obj files
if not exist build\obj\game mkdir build\obj\game
if not exist build\obj\editor mkdir build\obj\editor

rem src\shared has the code the game and the editor both use (levels, world, lighting, skybox, game data, keys)
set INCLUDES=/Isrc\shared /Ivendor\raylib\include /Ivendor\raygui /Ivendor\stb /Ivendor\miniz /Ivendor\json
set LIBS=/LIBPATH:vendor\raylib\lib raylib.lib winmm.lib gdi32.lib user32.lib shell32.lib opengl32.lib

rem Game: every .cpp in src\game and src\shared, so new files get built without editing this
echo Building game...
cl /nologo /std:c++17 /EHsc /O2 /MD %INCLUDES% src\game\*.cpp src\shared\*.cpp vendor\miniz\miniz.c /Fobuild\obj\game\ /Febuild\game.exe ^
    /link %LIBS% /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

rem entitylist.txt tells the editor every entity type the game has. It's written by the game itself
rem from its entity table (src\game\entities.cpp), so it's always up to date with this build
echo Exporting entitylist.txt...
build\game.exe -exportentities entitylist.txt >nul
if errorlevel 1 (echo Couldn't export entitylist.txt & exit /b 1)

rem Editor: every .cpp in src\editor and src\shared. comdlg32 is for the Open/Save file dialogs
echo Building editor...
cl /nologo /std:c++17 /EHsc /O2 /MD %INCLUDES% src\editor\*.cpp src\shared\*.cpp vendor\miniz\miniz.c /Fobuild\obj\editor\ /Febuild\editor.exe ^
    /link %LIBS% comdlg32.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

rem Pack the assets into gamedata.zip next to the executable, keeping the same folder layout.
rem Uses the tar.exe that comes with Windows, which can write zips
if exist build\gamedata.zip del build\gamedata.zip
"%SystemRoot%\System32\tar.exe" -a -c -f build\gamedata.zip cfg\*.cfg maps\*.json model\*.obj model\*.mtl textures
if errorlevel 1 exit /b 1

endlocal
