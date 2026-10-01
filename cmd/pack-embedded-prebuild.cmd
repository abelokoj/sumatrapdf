@echo off
REM VS / ninja prebuild helper: stages everything SumatraPDF.exe embeds under
REM out\<cfg>\ and packs it into one LzSA archive linked as IDR_EMBEDDED_PAK
REM (SumatraPDF.rc gets its path via the EMBEDDED_PAK resdefine, see premake5.lua).
REM
REM   pack-embedded-prebuild.cmd <staging dir> <archive> [<file>[:<in-archive name>] ...]
REM
REM   SumatraPDF-static:  out\<cfg>\embedded-static  ->  out\<cfg>\embedded-static.lzsa
REM   SumatraPDF:         out\<cfg>\embedded         ->  out\<cfg>\embedded.lzsa
REM                       plus libsumatrapdf.dll & co (the installer payload)
REM                       passed as extra files
REM
REM Staged (in-archive names are relative to the staging dir):
REM   .work\translations.txt  (created empty when trans-dl.ts never ran)
REM   ext\marked.min.js, ext\mermaid.min.js
REM   fonts\*                 mupdf's built-in fonts, picked from
REM                           ext\mupdf\resources\fonts (see :fonts below);
REM                           src\mupdf\noto_sumatra.c loads them by file name
REM   .work\docs\**           (in-app manual from gen-docs.ts; skipped when missing)
REM
REM Only cmd + MakeLZSA so MSBuild need not have bun on PATH. The archive is kept
REM between runs: MakeLZSA reuses unchanged entries, which keeps rebuilds fast.
REM
REM Do NOT permanently change the caller's cwd (MSBuild chains further
REM prebuild lines that assume the project directory).
setlocal
if "%~2"=="" (
  echo usage: pack-embedded-prebuild.cmd ^<staging dir^> ^<archive^> [^<file^>[:^<in-archive name^>] ...]
  exit /b 1
)
REM %~dp0 ends with a backslash, so strip it before appending "..": chopping the
REM last character off "<repo>\cmd\.." instead leaves "<repo>\cmd\" and every
REM path below then points inside cmd\ (MakeLZSA.exe "missing", the archive
REM never built, and the .rc fails with "file not found: ...\embedded.lzsa")
set CMDDIR=%~dp0
set ROOT=%CMDDIR:~0,-1%\..
set WORK=%ROOT%\.work
set FONTS=%ROOT%\ext\mupdf\resources\fonts
REM expand to full paths with backslashes (cmd expands %% even in REM lines, so
REM the modifier isn't spelled out here); the callers pass mixed vs2022-relative
REM slashes that rmdir / robocopy don't always accept.
set STAGING=%~f1
set ARCHIVE=%~f2

set EXTRA=
shift
:next_extra
shift
if "%~1"=="" goto extra_done
set EXTRA=%EXTRA% "%~1"
goto next_extra
:extra_done

if not exist "%ROOT%\bin\MakeLZSA.exe" (
  echo MakeLZSA.exe missing
  exit /b 1
)
if not exist "%WORK%" mkdir "%WORK%"
if not exist "%WORK%\translations.txt" type nul > "%WORK%\translations.txt"

REM mirror the manual first (/MIR also drops files no longer in .work\docs;
REM fonts\ is excluded so it survives); robocopy exit codes below 8 are success
if exist "%WORK%\docs\" (
  robocopy "%WORK%\docs" "%STAGING%" /MIR /XD "%STAGING%\fonts" /NFL /NDL /NJH /NJS /NP >nul
  if errorlevel 8 (
    echo robocopy "%WORK%\docs" "%STAGING%" failed
    exit /b 1
  )
) else (
  echo note: %WORK%\docs missing, packing without the manual ^(run: bun cmd\gen-docs.ts^)
  if exist "%STAGING%\" rmdir /s /q "%STAGING%"
)
if not exist "%STAGING%\" mkdir "%STAGING%"
copy /y "%WORK%\translations.txt" "%STAGING%\translations.txt" >nul || exit /b 1
copy /y "%ROOT%\ext\marked.min.js" "%STAGING%\marked.min.js" >nul || exit /b 1
copy /y "%ROOT%\ext\mermaid.min.js" "%STAGING%\mermaid.min.js" >nul || exit /b 1

REM Only runtime dictionary files and public license notices enter the installer.
if not exist "%STAGING%\dictionaries\wordnet-en\" mkdir "%STAGING%\dictionaries\wordnet-en"
for %%F in (adj.exc adv.exc data.adj data.adv data.noun data.verb LICENSE manifest.json noun.exc verb.exc) do (
  copy /y "%ROOT%\src\dictionaries\wordnet-en\%%F" "%STAGING%\dictionaries\wordnet-en\%%F" >nul || exit /b 1
)
if not exist "%STAGING%\docs\licenses\" mkdir "%STAGING%\docs\licenses"
for %%F in (Manrope-OFL.txt PretendardStd-OFL.txt PublicSans-OFL.txt lucide-LICENSE.txt vocabulary-wordlists-MIT.txt vocabulary-pack-attributions.json) do (
  copy /y "%ROOT%\docs\licenses\%%F" "%STAGING%\docs\licenses\%%F" >nul || exit /b 1
)
for %%F in (font-attribution.md icon-attribution.md vocabulary-attribution.md) do (
  copy /y "%ROOT%\docs\%%F" "%STAGING%\docs\%%F" >nul || exit /b 1
)
for %%F in (AUTHORS COPYING COPYING.BSD) do (
  copy /y "%ROOT%\%%F" "%STAGING%\%%F" >nul || exit /b 1
)
REM base 14 (URW), CJK fallback (Droid), Charis SIL for EPUB, a few Noto for
REM math / music / symbols / emoji. Not packed: NimbusBoxes, Source Han and the
REM per-script Noto fonts (font-table.h entries without a file are skipped).
call :fonts urw Dingbats.cff NimbusMonoPS-*.cff NimbusRoman-*.cff NimbusSans-*.cff StandardSymbolsPS.cff
if errorlevel 1 exit /b 1
call :fonts droid DroidSansFallbackFull.ttf
if errorlevel 1 exit /b 1
call :fonts sil CharisSIL*.cff
if errorlevel 1 exit /b 1
call :fonts noto NotoSans-Regular.otf NotoSerif-Regular.otf NotoSansMath-Regular.otf NotoMusic-Regular.otf
if errorlevel 1 exit /b 1
call :fonts noto NotoSansSymbols-Regular.otf NotoSansSymbols2-Regular.otf NotoEmoji-Regular.ttf
if errorlevel 1 exit /b 1

"%ROOT%\bin\MakeLZSA.exe" "%ARCHIVE%" "%STAGING%" %EXTRA%
exit /b %ERRORLEVEL%

REM :fonts <forge subdir of ext\mupdf\resources\fonts> <file or wildcard>...
REM robocopy skips files whose size and time are unchanged, so this is cheap
:fonts
set FORGE=%~1
shift
set FILES=
:next_font
if "%~1"=="" goto fonts_copy
set FILES=%FILES% "%~1"
shift
goto next_font
:fonts_copy
robocopy "%FONTS%\%FORGE%" "%STAGING%\fonts" %FILES% /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
  echo robocopy "%FONTS%\%FORGE%" "%STAGING%\fonts" failed
  exit /b 1
)
exit /b 0
