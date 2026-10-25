@echo off
setlocal
cd /d "%~dp0"
if not exist "build\geode-finder.exe" (
    echo Build first: pwsh -File build.ps1 -Test
    pause
    exit /b 1
)
"build\geode-finder.exe" --config finder.properties %*
if errorlevel 1 pause
