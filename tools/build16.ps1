# 16-bit real-mode/V86 DBOPL build (Watcom 1.9 wpp -mm -3) + hash test against the 32-bit build
# usage: powershell -ExecutionPolicy Bypass -File tools\build16.ps1
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
Remove-Item build16 -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force build16 | Out-Null
python tools\gen_dbopl16.py ref\VSBHDA\src build16\src
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"
$inc = "-I$root\build16\src -I$root\src"
$cf  = '-zq -bt=dos -mm -3 -ox -fpi87 -zp2'      # medium model, 386 instructions in 16-bit code, near data (<64KB)
cmd /c "wpp $cf -d0 $inc -fo=build16\dbopl16.obj build16\src\DBOPL.CPP";          if ($LASTEXITCODE) { throw 'DBOPL.CPP' }
cmd /c "wpp $cf $inc -fo=build16\dbopl_core16.obj src\dbopl_core.cpp";             if ($LASTEXITCODE) { throw 'dbopl_core' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox -I$root\src -fo=build16\gen_test16.obj test\gen_test.c"; if ($LASTEXITCODE) { throw 'gen_test' }
@('system dos','option quiet','option stack=8k','option map=build16\gen16.map','name build16\gen16.exe',
  'file build16\gen_test16.obj','file build16\dbopl_core16.obj','file build16\dbopl16.obj') | Set-Content build16\gen16.lnk -Encoding ASCII
cmd /c "wlink @build16\gen16.lnk"; if ($LASTEXITCODE) { throw 'wlink' }
Copy-Item build16\gen16.exe build\gen16.exe -Force
"OK: build16\gen16.exe  (expected PCM hash == 32-bit: OPL2/9ch C9CE7162, OPL3/18ch 21030ACB)"
