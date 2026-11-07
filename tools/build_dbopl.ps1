# DBOPL backend build (Watcom 1.9 native, no MinGW needed) + engine benchmarks
# usage: powershell -ExecutionPolicy Bypass -File tools\build_dbopl.ps1
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
New-Item -ItemType Directory -Force build | Out-Null
# int32-accumulating DBOPL copy (original VSBHDA int16 accumulation wraps on overload; upstream is not modified)
python tools\gen_dbopl32.py ref\VSBHDA\src build\gen\dbopl32
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"
$inc = "-I$root\build\gen\dbopl32 -I$root\src"
$cf  = '-zq -bt=dos -mf -5r -ox -fp5 -fpi87 -zp4'
# NOTE: DOS 8.3 - executable name must be <= 8 chars (gendbopl.exe)
cmd /c "wpp386 $cf -d0 $inc -fo=build\dbopl_wc.obj build\gen\dbopl32\DBOPL.CPP";   if ($LASTEXITCODE) { throw 'DBOPL.CPP' }
cmd /c "wpp386 $cf $inc -fo=build\dbopl_core_wc.obj src\dbopl_core.cpp";            if ($LASTEXITCODE) { throw 'dbopl_core' }
cmd /c "wcc386 -zq -bt=dos -mf -5r -ox -I$root\src -fo=build\gen_test_dos.obj test\gen_test.c"; if ($LASTEXITCODE) { throw 'gen_test' }
@('system dos4g','option quiet','option stack=64k','option map=build\gendbopl.map','name build\gendbopl.exe',
  'file build\gen_test_dos.obj','file build\dbopl_core_wc.obj','file build\dbopl_wc.obj') | Set-Content build\gendbopl.lnk -Encoding ASCII
cmd /c "wlink @build\gendbopl.lnk"; if ($LASTEXITCODE) { throw 'wlink' }
"OK: build\gendbopl.exe"
