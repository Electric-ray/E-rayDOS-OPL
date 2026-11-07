# Phase 2b build: spk16.exe = 16-bit real-mode DBOPL + PC speaker INT 8 driver (no DOS/4GW). Medium model, 386 instructions.
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
if (!(Test-Path build16\dbopl16.obj)) { & powershell -ExecutionPolicy Bypass -File tools\build16.ps1 }
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"
$inc = "-I$root\src"
# -s: no stack checks (ISR / private stack); -zu: SS != DGROUP not assumed (we keep SS==DS in the ISR anyway)
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -fo=build16\pcspk16.obj src\pcspk16.c";   if ($LASTEXITCODE) { throw 'pcspk16.c' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox $inc -fo=build16\spk16.obj test\spk16.c";          if ($LASTEXITCODE) { throw 'spk16.c' }
@('system dos','option quiet','option stack=4k','option map=build16\spk16.map','name build16\spk16.exe',
  'file build16\spk16.obj','file build16\pcspk16.obj','file build16\dbopl_core16.obj','file build16\dbopl16.obj') | Set-Content build16\spk16.lnk -Encoding ASCII
cmd /c "wlink @build16\spk16.lnk"; if ($LASTEXITCODE) { throw 'wlink' }
Copy-Item build16\spk16.exe build\spk16.exe -Force
"OK: build16\spk16.exe"
