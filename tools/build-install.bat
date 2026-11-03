@echo off
REM SPDX-License-Identifier: MIT
REM
REM Build AiBgRemover.ofx and install it into DaVinci Resolve's OFX plugin folder.
REM
REM   tools\build-install.bat                       build Release + verify + install
REM   tools\build-install.bat build                 build Release + verify, do NOT install
REM   tools\build-install.bat uninstall             remove the installed bundle
REM
REM Run from an "x64 Native Tools Command Prompt for VS 2026". If you run it from an ordinary
REM cmd/powershell window this script will try to locate vcvars64.bat and call it for you.
REM
REM INSTALLING NEEDS ADMINISTRATOR RIGHTS (it writes under Program Files) and RESOLVE MUST BE
REM COMPLETELY CLOSED - a loaded .ofx is file-locked and the copy will fail.

setlocal EnableExtensions EnableDelayedExpansion

REM -----------------------------------------------------------------------
REM Configuration - edit these two if your paths differ
REM -----------------------------------------------------------------------
if "%OPENFX_SDK_DIR%"=="" set "OPENFX_SDK_DIR=D:\DavinciCoding\OpenFX\openfx"
set "OFX_PLUGINS_DIR=%CommonProgramFiles%\OFX\Plugins"
set "BUNDLE_NAME=AiBgRemover.ofx.bundle"
set "CONFIG=Release"
set "GENERATOR=Visual Studio 18 2026"

REM Repo root is the parent of this script's folder.
set "REPO_ROOT=%~dp0.."
pushd "%REPO_ROOT%" >nul
set "REPO_ROOT=%CD%"
popd >nul

echo.
echo === AiBgRemover build-install =====================================
echo   repo root      : %REPO_ROOT%
echo   OpenFX SDK     : %OPENFX_SDK_DIR%
echo   configuration  : %CONFIG%
echo   generator      : %GENERATOR%
echo ==================================================================
echo.

REM -----------------------------------------------------------------------
REM uninstall
REM -----------------------------------------------------------------------
if /I "%~1"=="uninstall" goto :do_uninstall

REM -----------------------------------------------------------------------
REM 0. Sanity checks
REM -----------------------------------------------------------------------
if not exist "%OPENFX_SDK_DIR%\include\ofxImageEffect.h" (
    echo ERROR: cannot find "%OPENFX_SDK_DIR%\include\ofxImageEffect.h".
    echo        Set OPENFX_SDK_DIR to your OpenFX checkout, e.g.
    echo            set OPENFX_SDK_DIR=D:\DavinciCoding\OpenFX\openfx
    echo        and run this script again.
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake is not on PATH. Install it, or run this from a developer prompt.
    exit /b 1
)

REM If cl.exe is missing we are not in a developer prompt - try to find vcvars64.bat.
where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe not on PATH - looking for vcvars64.bat ...
    set "VCVARS="
    for %%D in (
        "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\18\*\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\18\*\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat"
    ) do if exist %%D if not defined VCVARS set "VCVARS=%%~D"
    if not defined VCVARS (
        echo ERROR: could not find vcvars64.bat. Open an "x64 Native Tools Command Prompt"
        echo        and run this script from there.
        exit /b 1
    )
    echo   found: !VCVARS!
    call "!VCVARS!" >nul
    if errorlevel 1 (
        echo ERROR: vcvars64.bat failed.
        exit /b 1
    )
)

REM -----------------------------------------------------------------------
REM 1. Configure
REM -----------------------------------------------------------------------
echo.
echo [1/5] Configuring ...
cmake -S "%REPO_ROOT%" -B "%REPO_ROOT%\build" -G "%GENERATOR%" -A x64 ^
      -DOPENFX_SDK_DIR="%OPENFX_SDK_DIR%" ^
      -DBUILD_TESTS=ON ^
      -DOFX_STATIC_CRT=ON ^
      -DOFX_DEFAULT_LOG_LEVEL=Info ^
      -DAIBGR_WORK_DIR="%REPO_ROOT%"
if errorlevel 1 (
    echo.
    echo CONFIGURE FAILED. If the error mentions the generator name, list what CMake
    echo knows about with:   cmake --help
    echo and set GENERATOR at the top of this script to one it prints, e.g.
    echo   "Visual Studio 17 2022"  or  "Ninja"
    exit /b 1
)

REM -----------------------------------------------------------------------
REM 2. Build
REM -----------------------------------------------------------------------
echo.
echo [2/5] Building ...
cmake --build "%REPO_ROOT%\build" --config %CONFIG% --parallel
if errorlevel 1 (
    echo BUILD FAILED.
    echo.
    echo   If the error is LNK2038 ^(RuntimeLibrary mismatch^), this build directory still
    echo   holds object files compiled before the MSVC runtime was set project-wide, and
    echo   MSBuild relinked them instead of recompiling. Delete it and re-run:
    echo.
    echo       rmdir /s /q "%REPO_ROOT%\build"
    echo       tools\build-install.bat
    exit /b 1
)

REM -----------------------------------------------------------------------
REM 3. Unit tests
REM -----------------------------------------------------------------------
echo.
echo [3/5] Running unit tests ...
ctest --test-dir "%REPO_ROOT%\build" -C %CONFIG% --output-on-failure
if errorlevel 1 (
    echo.
    echo TESTS FAILED. Not installing - fix these first.
    exit /b 1
)

REM -----------------------------------------------------------------------
REM 4. Stage the bundle and verify the binary
REM -----------------------------------------------------------------------
echo.
echo [4/5] Staging bundle and verifying exports ...
if exist "%REPO_ROOT%\build\bundle" rmdir /s /q "%REPO_ROOT%\build\bundle"
cmake --install "%REPO_ROOT%\build" --config %CONFIG% --prefix "%REPO_ROOT%\build\bundle"
if errorlevel 1 (
    echo INSTALL/STAGING FAILED.
    exit /b 1
)

set "STAGED_OFX=%REPO_ROOT%\build\bundle\%BUNDLE_NAME%\Contents\Win64\AiBgRemover.ofx"
if not exist "%STAGED_OFX%" (
    echo ERROR: expected the staged binary at
    echo   %STAGED_OFX%
    echo but it is not there.
    exit /b 1
)

echo.
set "EXPORTS_TXT=%TEMP%\aibgr_exports.txt"
dumpbin /exports "%STAGED_OFX%" > "%EXPORTS_TXT%" 2>&1
type "%EXPORTS_TXT%" | findstr /i "OfxGet"
echo.

REM An empty or decorated export table is the classic "Resolve loads the binary and reports
REM no plugins" failure, so it is a hard stop rather than something to eyeball. The names must
REM end the line: a leading underscore or an @n suffix means C++ mangling leaked through.
set "EXPORT_FAIL="
findstr /r /c:"OfxGetNumberOfPlugins *$" "%EXPORTS_TXT%" >nul || set "EXPORT_FAIL=OfxGetNumberOfPlugins"
findstr /r /c:"OfxGetPlugin *$"          "%EXPORTS_TXT%" >nul || set "EXPORT_FAIL=%EXPORT_FAIL% OfxGetPlugin"
if defined EXPORT_FAIL (
    echo.
    echo *** EXPORT CHECK FAILED: missing or decorated:%EXPORT_FAIL%
    echo     Resolve will load this binary and report that it contains no plugins.
    echo     plugin/OfxEntry.cpp exports both entry points through its own macro, so a
    echo     failure here means something stripped the export table - check that
    echo     /WHOLEARCHIVE or a .def file is not interfering, and paste the dumpbin
    echo     output above when reporting it.
    del "%EXPORTS_TXT%" 2>nul
    exit /b 1
)
echo Export check PASSED: both entry points are present and undecorated.
del "%EXPORTS_TXT%" 2>nul

echo.
echo --- dumpbin /dependents ---
dumpbin /dependents "%STAGED_OFX%" | findstr /i ".dll"

echo.
echo The dependents list above should contain KERNEL32.dll and nothing else while the
echo CRT is linked statically. VCRUNTIME140.dll or MSVCP140.dll means -DOFX_STATIC_CRT=ON
echo did not apply and the plugin will fail to load on a machine without that
echo redistributable. Once ONNX Runtime is added in Phase 3 its DLLs appear here too and
echo must be copied into the bundle alongside the .ofx.
echo.

if /I "%~1"=="build" (
    echo Build-only mode: not installing. Staged bundle is at
    echo   %REPO_ROOT%\build\bundle\%BUNDLE_NAME%
    exit /b 0
)

REM -----------------------------------------------------------------------
REM 5. Install into Resolve
REM -----------------------------------------------------------------------
echo [5/5] Installing into Resolve ...

tasklist /fi "imagename eq Resolve.exe" 2>nul | find /i "Resolve.exe" >nul
if not errorlevel 1 (
    echo.
    echo ERROR: DaVinci Resolve is RUNNING. Close it completely - not just the
    echo        window, check Task Manager - and run this again. A loaded .ofx is
    echo        file-locked and the copy will silently fail or half-succeed.
    exit /b 1
)

if not exist "%OFX_PLUGINS_DIR%" (
    echo   creating %OFX_PLUGINS_DIR%
    mkdir "%OFX_PLUGINS_DIR%" 2>nul
    if errorlevel 1 (
        echo ERROR: cannot create "%OFX_PLUGINS_DIR%".
        echo        Re-run this script from an ADMINISTRATOR command prompt.
        exit /b 1
    )
)

set "PLUGIN_DATA=%ProgramData%\AiBgRemover"
call :backup_bundle_log
if errorlevel 1 exit /b 1

echo   removing any previous install
if exist "%OFX_PLUGINS_DIR%\%BUNDLE_NAME%" rmdir /s /q "%OFX_PLUGINS_DIR%\%BUNDLE_NAME%"

echo   copying %BUNDLE_NAME%
xcopy /e /i /q /y "%REPO_ROOT%\build\bundle\%BUNDLE_NAME%" "%OFX_PLUGINS_DIR%\%BUNDLE_NAME%" >nul
if errorlevel 1 (
    echo.
    echo COPY FAILED. This almost always means no administrator rights on
    echo   %OFX_PLUGINS_DIR%
    echo Right-click your terminal -^> "Run as administrator", and run this again.
    exit /b 1
)

REM Force Resolve to rebuild its plugin list on next launch.
set "RESOLVE_SUPPORT=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support"
if exist "%RESOLVE_SUPPORT%\OFXPluginCacheV2.xml" (
    echo   resetting Resolve's OFX plugin cache
    ren "%RESOLVE_SUPPORT%\OFXPluginCacheV2.xml" "OFXPluginCacheV2.xml.old" 2>nul
)

REM The requested workspace is normally writable by the user running Resolve.
set "INSTALLED_LOG=%REPO_ROOT%\log.txt"

echo.
echo ==================================================================
echo  INSTALLED:  %OFX_PLUGINS_DIR%\%BUNDLE_NAME%
echo  PREFERRED LOG: %INSTALLED_LOG%
echo  FALLBACK:   %PLUGIN_DATA%\log.txt or %%LOCALAPPDATA%%\AiBgRemover\log.txt
echo.
echo  Now start DaVinci Resolve and:
echo    1. Effects -^> OpenFX -^> AI Matting -^> AI Background Remover
echo    2. Drag it onto a clip on the Edit page
echo    3. Check all six tabs appear in the Inspector
echo    4. Read the log file above and look for the "host:" lines
echo.
echo  If it does not appear in the Effects list, see
echo  docs\05-build-install-test-windows.md section 3.
echo ==================================================================
exit /b 0

REM -----------------------------------------------------------------------
:do_uninstall
echo Uninstalling ...
tasklist /fi "imagename eq Resolve.exe" 2>nul | find /i "Resolve.exe" >nul
if not errorlevel 1 (
    echo ERROR: close DaVinci Resolve first.
    exit /b 1
)
set "PLUGIN_DATA=%ProgramData%\AiBgRemover"
call :backup_bundle_log
if errorlevel 1 exit /b 1
if exist "%OFX_PLUGINS_DIR%\%BUNDLE_NAME%" (
    rmdir /s /q "%OFX_PLUGINS_DIR%\%BUNDLE_NAME%"
    if errorlevel 1 (
        echo ERROR: could not remove it. Re-run as administrator.
        exit /b 1
    )
    echo   removed %OFX_PLUGINS_DIR%\%BUNDLE_NAME%
) else (
    echo   nothing installed at %OFX_PLUGINS_DIR%\%BUNDLE_NAME%
)
set "RESOLVE_SUPPORT=%APPDATA%\Blackmagic Design\DaVinci Resolve\Support"
if exist "%RESOLVE_SUPPORT%\OFXPluginCacheV2.xml" (
    ren "%RESOLVE_SUPPORT%\OFXPluginCacheV2.xml" "OFXPluginCacheV2.xml.old" 2>nul
    echo   reset Resolve's OFX plugin cache
)
echo Done. Any existing bundle log was backed up in %PLUGIN_DATA%.
exit /b 0

:backup_bundle_log
set "OLD_LOG=%OFX_PLUGINS_DIR%\%BUNDLE_NAME%\Contents\Win64\log.txt"
if not exist "%OLD_LOG%" exit /b 0
if not exist "%PLUGIN_DATA%" mkdir "%PLUGIN_DATA%" 2>nul
if not exist "%PLUGIN_DATA%" (
    echo ERROR: Cannot create %PLUGIN_DATA% to back up the current log. Install stopped.
    exit /b 1
)
:choose_log_backup
set "LOG_BACKUP=%PLUGIN_DATA%\previous-log-%RANDOM%-%RANDOM%.txt"
if exist "%LOG_BACKUP%" goto :choose_log_backup
copy /y "%OLD_LOG%" "%LOG_BACKUP%" >nul
if errorlevel 1 (
    echo ERROR: Could not back up the current plugin log. Install stopped.
    exit /b 1
)
echo   backed up plugin log to %LOG_BACKUP%
exit /b 0
