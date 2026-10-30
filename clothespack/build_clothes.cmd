@echo off
rem TLDCoop_DLC: builds tldcoop_clothes.bundle (friends' clothing for their avatars) from the clothing catalog,
rem and renders preview pictures of a few bodies wearing it.
rem   build_clothes.cmd "D:\path\to\TLD_Odziez_Pelny_Katalog" "D:\SteamLibrary\steamapps\common\TheLongDark"
rem The result contains Hinterland's art: for you and friends who own the game. Never publish it.
setlocal
set HERE=%~dp0
set CATALOG=%~1
set GAME=%~2
if "%CATALOG%"=="" set CATALOG=%HERE%TLD_Odziez_Pelny_Katalog
if "%GAME%"=="" set GAME=D:\SteamLibrary\steamapps\common\TheLongDark
set UNITY=C:\Program Files\Unity\Hub\Editor\6000.0.60f1\Editor\Unity.exe
if not exist "%UNITY%" set UNITY=D:\Unity\6000.0.60f1\Editor\Unity.exe
set PROJECT=%HERE%project

if not exist "%UNITY%" (echo Unity 6000.0.60f1 not found. Install it with Unity Hub. & pause & exit /b 1)
if not exist "%CATALOG%\characters" (echo Clothing catalog not found at %CATALOG% & pause & exit /b 1)
if not exist "%GAME%\TLDCoop\tldcoop_bodies.bundle" (echo tldcoop_bodies.bundle not found in %GAME%\TLDCoop & pause & exit /b 1)

if not exist "%PROJECT%\Assets" (
  echo Creating the Unity project - a few minutes the first time...
  "%UNITY%" -batchmode -nographics -quit -createProject "%PROJECT%" -logFile "%HERE%create.log" || (echo Project creation failed, see create.log & pause & exit /b 1)
)
if not exist "%PROJECT%\Assets\Editor" mkdir "%PROJECT%\Assets\Editor"
if not exist "%PROJECT%\Assets\BodyPackShaders" mkdir "%PROJECT%\Assets\BodyPackShaders"
copy /y "%HERE%Editor\BuildClothes.cs" "%PROJECT%\Assets\Editor\" >nul
copy /y "%HERE%Shaders\*.shader" "%PROJECT%\Assets\BodyPackShaders\" >nul
copy /y "%HERE%clothes.json" "%PROJECT%\clothes.json" >nul
if not exist "%PROJECT%\bodycut" mkdir "%PROJECT%\bodycut"
if exist "%HERE%bodycut" copy /y "%HERE%bodycut\*.bodycut" "%PROJECT%\bodycut\" >nul
> "%PROJECT%\catalog.txt" echo %CATALOG%
> "%PROJECT%\bodies.txt" echo %GAME%\TLDCoop\tldcoop_bodies.bundle

echo Building the clothes bundle - the first run imports the textures, it can take 10+ minutes...
"%UNITY%" -batchmode -nographics -quit -projectPath "%PROJECT%" -executeMethod TLDCoop.BuildClothes.Build -logFile "%HERE%build.log"
if errorlevel 1 (echo Build failed, see build.log & pause & exit /b 1)
findstr /c:"[Clothes]" "%HERE%build.log"
copy /y "%PROJECT%\Build\tldcoop_clothes" "%HERE%tldcoop_clothes.bundle" >nul || (pause & exit /b 1)

echo Rendering previews...
if exist "%PROJECT%\previews" rmdir /s /q "%PROJECT%\previews"
"%UNITY%" -batchmode -quit -projectPath "%PROJECT%" -executeMethod TLDCoop.BuildClothes.Preview -logFile "%HERE%preview.log"
if errorlevel 1 (echo Preview failed, see preview.log & pause & exit /b 1)
findstr /c:"[Clothes]" "%HERE%preview.log"
if exist "%HERE%previews" rmdir /s /q "%HERE%previews"
xcopy /e /i /q "%PROJECT%\previews" "%HERE%previews" >nul
echo.
echo Done: tldcoop_clothes.bundle and the previews folder are next to this file.
pause
