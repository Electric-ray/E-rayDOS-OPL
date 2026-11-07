# E-ray DOS OPL Emulator - build script (Phase 1: core + test)
# usage: powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Target host|dos|all]
param([string]$Target = 'all', [string]$Watcom = 'C:\WATCOM19')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
New-Item -ItemType Directory -Force build\gen | Out-Null

# 1) generate YMFM OPL-only subset (upstream is NOT modified)
python tools\gen_min.py ref\ymfm\src\ymfm_opl.cpp build\gen\ymfm_opl_min.cpp

# 2) core objects compiled with MinGW g++ (C++14, freestanding, i586, no SSE)
$cf = '-std=c++14','-Os','-m32','-march=i586','-ffreestanding','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics',
      '-fno-use-cxa-atexit','-ffunction-sections','-fdata-sections','-fno-stack-protector','-mno-sse','-mno-mmx','-DNDEBUG',
      '-I',"$root\ref\ymfm\src",'-I',"$root\src"
function CXX($src,$obj,$extra=@()) {
    $o = & g++ @cf @extra -c $src -o $obj 2>&1
    if ($LASTEXITCODE) { $o | Select-Object -First 30; throw "compile failed: $src" }
}
CXX src\opl_core.cpp build\opl_core.o
CXX build\gen\ymfm_opl_min.cpp build\ymfm_opl_min.o
CXX src\rt_stubs.cpp build\rt_stubs_host.o
CXX src\rt_stubs.cpp build\rt_stubs_dos.o @('-DOPLC_FREESTANDING')

if ($Target -in 'host','all') {
    & gcc -m32 -O2 -I src test\gen_test.c build\opl_core.o build\ymfm_opl_min.o build\rt_stubs_host.o -lgcc `
        -o build\gen_test_host.exe '-Wl,--gc-sections'
    if ($LASTEXITCODE) { throw 'host link failed' }
    "host build OK: build\gen_test_host.exe"
}

if ($Target -in 'dos','all') {
    $env:WATCOM = $Watcom
    $env:PATH = "$Watcom\binnt;$Watcom\binw;" + $env:PATH
    $env:INCLUDE = "$Watcom\h"
    # Watcom side: only C test driver (DOS/4GW flat 32bit); core objects are COFF from MinGW
    $inc = "-I$root\src"
    cmd /c "wcc386 -zq -bt=dos -mf -5r -ox $inc -fo=build\gen_test_dos.obj test\gen_test.c"   # via cmd: PowerShell mangles -5r/-fo= args
    if ($LASTEXITCODE) { throw 'wcc386 failed' }
    $lnk = @(
        'system dos4g', 'option quiet', 'option stack=32k', 'option map=build\gen_test_dos.map',
        'name build\gen_test.exe',
        'file build\gen_test_dos.obj', 'file build\opl_core.o', 'file build\ymfm_opl_min.o', 'file build\rt_stubs_dos.o'
    )
    Set-Content build\gen_test_dos.lnk $lnk -Encoding ASCII
    cmd /c "wlink @build\gen_test_dos.lnk"
    if ($LASTEXITCODE) { throw 'wlink failed' }
    "dos build OK: build\gen_test.exe"
}
