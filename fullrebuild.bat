@echo off
rem Full rebuild of Godot for Windows with .NET (C#) support,
rem including export templates (debug + release).
rem Equivalent of the Nix flake's "fullrebuild" script.

rem Unique per-commit version status, matching the system-flake convention:
rem   suffix = "schnozzlecat-" + first 4 chars of the commit hash
rem This must stay in sync with the Nix side (flake.nix and the system flake),
rem so both platforms generate identical NuGet package versions per commit.
rem Override by setting GODOT_VERSION_STATUS explicitly before running.
if not defined GODOT_VERSION_STATUS (
    for /f "delims=" %%H in ('git rev-parse HEAD') do set "GODOT_VERSION_STATUS=schnozzlecat-%%H"
)
if not defined GODOT_VERSION_STATUS goto :error
rem Truncate hash to 4 chars: schnozzlecat-<first4>
set "GODOT_VERSION_STATUS=%GODOT_VERSION_STATUS:~0,17%"

rem Local NuGet source the built packages are pushed to. The folder name is
rem deliberately stable across commits (register it once); the package
rem VERSIONS inside it change per commit, which is what NuGet distinguishes.
rem Register once with:
rem   dotnet nuget add source "%CD%\schnozzlecat-local" --name schnozzlecat-local
set "NUGET_SOURCE_DIR=%CD%\schnozzlecat-local"
if not exist "%NUGET_SOURCE_DIR%" mkdir "%NUGET_SOURCE_DIR%"

echo Building Godot editor with version status %GODOT_VERSION_STATUS%...
scons p=windows target=editor precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=no || goto :error

bin\godot.windows.editor.x86_64.mono.exe --headless --generate-mono-glue modules/mono/glue || goto :error

scons p=windows target=editor precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=yes || goto :error

echo Building export templates (debug)...
scons p=windows target=template_debug precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=yes || goto :error

echo Building export templates (release)...
scons p=windows target=template_release precision=single module_mono_enabled=yes module_text_server_fb_enabled=yes mono_glue=yes || goto :error

python modules\mono\build_scripts\build_assemblies.py --godot-output-dir bin --precision=single --push-nupkgs-local "%NUGET_SOURCE_DIR%" || goto :error

rem Get the editor's version string for the templates directory name.
rem NOTE: the templates dir uses FULL_CONFIG ("<number>.<status><module_config>", e.g.
rem "4.7.SchnozzleCat-custom.mono"), while --version prints the full build name which
rem additionally appends ".custom_build.<hash>". Strip those last two components.
for /f "delims=" %%V in ('bin\godot.windows.editor.x86_64.mono.exe --version') do set "GODOT_VERSION=%%V"
if "%GODOT_VERSION%"=="" goto :error
for /f "delims=" %%V in ('python -c "import sys; s=sys.argv[1].split(' v',1)[-1]; p=s.split('.'); print('.'.join(p[:-2]))" "%GODOT_VERSION%"') do set "GODOT_VERSION=%%V"
if "%GODOT_VERSION%"=="" goto :error

set "TEMPLATE_DIR=%APPDATA%\Godot\export_templates\%GODOT_VERSION%"
echo Installing export templates to %TEMPLATE_DIR%...
if not exist "%TEMPLATE_DIR%" mkdir "%TEMPLATE_DIR%"

copy /y "bin\godot.windows.template_debug.x86_64.mono.exe" "%TEMPLATE_DIR%\windows_debug_x86_64.exe" || goto :error
copy /y "bin\godot.windows.template_release.x86_64.mono.exe" "%TEMPLATE_DIR%\windows_release_x86_64.exe" || goto :error

rem Copy the template's C# API assemblies (needed for C# game exports).
if exist "bin\GodotSharp\Api" (
    robocopy "bin\GodotSharp\Api" "%TEMPLATE_DIR%\GodotSharp\Api" /e /nfl /ndl /njh /njs >nul
    if errorlevel 8 goto :error
)

rem Also install the mono glue copy the templates expect alongside.
if exist "bin\GodotSharp\Mono" (
    robocopy "bin\GodotSharp\Mono" "%TEMPLATE_DIR%\GodotSharp\Mono" /e /nfl /ndl /njh /njs >nul
    if errorlevel 8 goto :error
)

echo Full rebuild completed successfully.
echo Packages pushed to local NuGet source: %NUGET_SOURCE_DIR%
echo Templates installed to: %TEMPLATE_DIR%
exit /b 0

:error
echo Build failed with error code %ERRORLEVEL%.
exit /b %ERRORLEVEL%
