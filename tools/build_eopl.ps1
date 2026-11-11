# Phase 3a build: EOPL.EXE (resident TSR) + EOPLTEST.EXE (client). 16-bit medium model, Watcom 1.9.
# Engine variants (all verified bit-identical for OPL2 against the full engine by tools\test_variants.ps1):
#   build16   : full OPL2+OPL3 engine, FP at init   (tests, gen16.exe)
#   build16o2 : OPL2-only engine, FP at init         (gen16o2.exe, baseline for the no-FP variant)
#   build16nf : OPL2-only engine, NO floating point  (precomputed tables)  -> used by the TSR
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
& powershell -ExecutionPolicy Bypass -File tools\build16.ps1          # full engine (OPL2+OPL3) for tests
if ($LASTEXITCODE) { throw 'build16' }
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"

function BuildVariant($dir, $opts, $fp) {
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $dir | Out-Null
    python tools\gen_dbopl16.py ref\VSBHDA\src "$dir\src" $opts
    if ($LASTEXITCODE) { throw "gen_dbopl16 $opts" }
    $inc2 = "-I$root\$dir\src -I$root\src"
    $cf   = "-zq -bt=dos -mm -3 -ox -zp2 $fp"
    cmd /c "wpp $cf -d0 $inc2 -fo=$dir\dbopl16.obj $dir\src\DBOPL.CPP";                      if ($LASTEXITCODE) { throw "DBOPL.CPP $dir" }
    cmd /c "wpp $cf -dEOPL_OPL2_ONLY $inc2 -fo=$dir\dbopl_core16.obj src\dbopl_core.cpp";     if ($LASTEXITCODE) { throw "dbopl_core $dir" }
    cmd /c "wcc -zq -bt=dos -mm -3 -ox -I$root\src -fo=$dir\gen_test16.obj test\gen_test.c";  if ($LASTEXITCODE) { throw "gen_test $dir" }
    $name = 'gen16' + ($dir -replace 'build16', '')
    @('system dos','option quiet','option stack=8k',"name $dir\$name.exe",
      "file $dir\gen_test16.obj","file $dir\dbopl_core16.obj","file $dir\dbopl16.obj") | Set-Content "$dir\$name.lnk" -Encoding ASCII
    cmd /c "wlink @$dir\$name.lnk"; if ($LASTEXITCODE) { throw "wlink $name" }
    Copy-Item "$dir\$name.exe" build\ -Force
}
BuildVariant 'build16o2' 'opl2'      '-fpi87'
BuildVariant 'build16nf' 'opl2,nofp' ''

# TSR (no-FP engine; no -fpi87 anywhere)
$inc = "-I$root\src"
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -dEOPL_PDM -fo=build16nf\pcspk16.obj src\pcspk16.c";  if ($LASTEXITCODE) { throw 'pcspk16.c' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -fo=build16nf\eopl.obj src\eopl.c";        if ($LASTEXITCODE) { throw 'eopl.c' }
cmd /c "wasm -zq -fo=build16nf\trapglue.obj src\trapglue.asm";                         if ($LASTEXITCODE) { throw 'trapglue.asm' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -fo=build16\eopltest.obj test\eopltest.c"; if ($LASTEXITCODE) { throw 'eopltest.c' }
@('system dos','option quiet','option stack=1536','option map=build16nf\eopl.map','name build16nf\eopl.exe',
  'file build16nf\eopl.obj','file build16nf\trapglue.obj','file build16nf\pcspk16.obj','file build16nf\dbopl_core16.obj','file build16nf\dbopl16.obj') | Set-Content build16nf\eopl.lnk -Encoding ASCII
cmd /c "wlink @build16nf\eopl.lnk"; if ($LASTEXITCODE) { throw 'wlink eopl' }
@('system dos','option quiet','option stack=4k','name build16\eopltest.exe','file build16\eopltest.obj') | Set-Content build16\eopltest.lnk -Encoding ASCII
cmd /c "wlink @build16\eopltest.lnk"; if ($LASTEXITCODE) { throw 'wlink eopltest' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -fo=build16\adlplay.obj test\adlplay.c";   if ($LASTEXITCODE) { throw 'adlplay.c' }
@('system dos','option quiet','option stack=4k','name build16\adlplay.exe','file build16\adlplay.obj') | Set-Content build16\adlplay.lnk -Encoding ASCII
cmd /c "wlink @build16\adlplay.lnk"; if ($LASTEXITCODE) { throw 'wlink adlplay' }
Copy-Item build16nf\eopl.exe,build16\eopltest.exe,build16\adlplay.exe build\ -Force
"OK: build16nf\eopl.exe, build16\eopltest.exe, build16\adlplay.exe, build\gen16o2.exe, build\gen16nf.exe"
