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
rem The game and the editor both build src\engine and src\shared, so each gets its own folder of .obj files
if not exist build\obj\game mkdir build\obj\game
if not exist build\obj\editor mkdir build\obj\editor

rem src\engine is the engine: rendering, the world, game data, the console, cvars, commands and binds.
rem It knows nothing about the game, which adds its own commands to it.
rem src\shared is what the editor shares with the game or the engine: the level format and key names
set INCLUDES=/Isrc\engine /Isrc\shared /Ivendor\raylib\include /Ivendor\raygui /Ivendor\stb /Ivendor\miniz /Ivendor\json
set LIBS=/LIBPATH:vendor\raylib\lib raylib.lib winmm.lib gdi32.lib user32.lib shell32.lib opengl32.lib

rem The icon for both programs. rc comes with the same Visual Studio setup as cl
rc /nologo /fobuild\obj\icon.res icon.rc
if errorlevel 1 exit /b 1

rem Game: every .cpp in src\game, src\engine and src\shared, so new files get built without editing this
echo Building game...
cl /nologo /std:c++17 /EHsc /O2 /MD %INCLUDES% src\game\*.cpp src\engine\*.cpp src\shared\*.cpp vendor\miniz\miniz.c /Fobuild\obj\game\ /Febuild\grimflower.exe ^
    /link %LIBS% build\obj\icon.res /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

rem entitylist.txt tells the editor every entity type the game has. It's written by the game itself
rem from its entity table (src\game\entities.cpp), so it's always up to date with this build
echo Exporting entitylist.txt...
build\grimflower.exe -exportentities entitylist.txt >nul
if errorlevel 1 (echo Couldn't export entitylist.txt & exit /b 1)

rem Editor: every .cpp in src\editor, src\engine and src\shared. comdlg32 is for the Open/Save file dialogs
echo Building editor...
cl /nologo /std:c++17 /EHsc /O2 /MD %INCLUDES% src\editor\*.cpp src\engine\*.cpp src\shared\*.cpp vendor\miniz\miniz.c /Fobuild\obj\editor\ /Febuild\GrimEd.exe ^
    /link %LIBS% build\obj\icon.res comdlg32.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

rem Pack the assets into gamedata.zip next to the executable, keeping the same folder layout.
rem Uses the tar.exe that comes with Windows, which can write zips
rem sound\ (world sounds) and music\ are only packed once they exist
if exist build\gamedata.zip del build\gamedata.zip
set ASSETS=cfg\*.cfg maps\*.json models textures
if exist sound set ASSETS=%ASSETS% sound
if exist music set ASSETS=%ASSETS% music
rem models\ keeps its subfolders and the textures next to the models, but not Blender files or autosaves
rem Bounce lightmaps the editor saved next to the levels
if exist maps\*_lightmap.png set ASSETS=%ASSETS% maps\*_lightmap.png
rem Materials (bump, specular and gloss maps for each texture), written by the editor's Materials tab
if exist .materials set ASSETS=%ASSETS% .materials
"%SystemRoot%\System32\tar.exe" -a -c -f build\gamedata.zip --exclude=*.blend --exclude=*.blend1 --exclude=models/autosave %ASSETS%
if errorlevel 1 exit /b 1

endlocal
