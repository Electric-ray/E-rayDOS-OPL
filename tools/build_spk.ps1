# Phase 2 build: spktest.exe (DBOPL + PC speaker driver), Watcom 1.9 only
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"
if (!(Test-Path build\dbopl_wc.obj)) { & powershell -ExecutionPolicy Bypass -File tools\build_dbopl.ps1 }
$inc = "-I$root\src"
# -s: no stack-overflow checks (ISR safety)
cmd /c "wcc386 -zq -bt=dos -mf -5r -ox -s $inc -fo=build\pcspk.obj src\pcspk.c";      if ($LASTEXITCODE) { throw 'pcspk.c' }
cmd /c "wcc386 -zq -bt=dos -mf -5r -ox $inc -fo=build\spktest.obj test\spktest.c";     if ($LASTEXITCODE) { throw 'spktest.c' }
@('system dos4g','option quiet','option stack=64k','option map=build\spktest.map','name build\spktest.exe',
  'file build\spktest.obj','file build\pcspk.obj','file build\dbopl_core_wc.obj','file build\dbopl_wc.obj') | Set-Content build\spktest.lnk -Encoding ASCII
cmd /c "wlink @build\spktest.lnk"; if ($LASTEXITCODE) { throw 'wlink' }
"OK: build\spktest.exe"
