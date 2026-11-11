# noise lab (real-hardware A/B listening) built on the 16-bit driver
param([string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; Set-Location $root
if (!(Test-Path build16\dbopl16.obj)) { & powershell -ExecutionPolicy Bypass -File tools\build16.ps1 }
$env:WATCOM = $Watcom; $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH; $env:INCLUDE = "$Watcom\h"
$inc = "-I$root\src"
# -s: no stack checks (ISR / private stack); -zu: SS != DGROUP not assumed (we keep SS==DS in the ISR anyway)
cmd /c "wcc -zq -bt=dos -mm -3 -ox -s $inc -dNLAB_DEBUG -dEOPL_PDM -fo=build16\pcspk16_lab.obj src\pcspk16.c";   if ($LASTEXITCODE) { throw 'pcspk16.c' }
cmd /c "wcc -zq -bt=dos -mm -3 -ox $inc -fo=build16\nlab.obj test\nlab.c";          if ($LASTEXITCODE) { throw 'nlab.c' }
@('system dos','option quiet','option stack=4k','option map=build16\nlab.map','name build16\nlab.exe',
  'file build16\nlab.obj','file build16\pcspk16_lab.obj','file build16\dbopl_core16.obj','file build16\dbopl16.obj') | Set-Content build16\nlab.lnk -Encoding ASCII
cmd /c "wlink @build16\nlab.lnk"; if ($LASTEXITCODE) { throw 'wlink' }
Copy-Item build16\nlab.exe build\nlab.exe -Force
"OK: build16\nlab.exe"


