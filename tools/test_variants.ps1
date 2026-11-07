# Verifies that every engine variant produces bit-identical PCM (OPL2, 9 channels) at all supported rates, in DOSBox-X.
#   gen16 (full, FP)  vs  gen16o2 (OPL2-only, FP)  vs  gen16nf (OPL2-only, no FP, precomputed tables)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent; $b = "$root\build"
$rates = 8000, 11025, 16000, 22050
$exes  = 'gen16', 'gen16o2', 'gen16nf'
$lines = @('@echo off', "mount c $b", 'c:', 'echo start > tv.txt')
foreach ($r in $rates) { foreach ($e in $exes) { $lines += "echo $e $r >> tv.txt"; $lines += "$e opl2 $r 3 tv.wav 9 >> tv.txt" } }
$lines += 'exit'
$conf = "[sdl]`r`noutput=surface`r`n[dosbox]`r`nmemsize=32`r`n[cpu]`r`ncore=normal`r`ncycles=fixed 400000`r`n[autoexec]`r`n" + ($lines -join "`r`n")
Set-Content "$b\tv.conf" $conf -Encoding ASCII
Get-Process dosbox-x -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item "$b\tv.txt" -ErrorAction SilentlyContinue
$p = Start-Process C:\DOSBox-X\dosbox-x.exe -ArgumentList @('-conf', "$b\tv.conf", '-fastlaunch', '-nopromptfolder', '-nolog') -PassThru
if (!$p.WaitForExit(240000)) { 'TIMEOUT'; $p.Kill() }
Start-Sleep 1
$txt = (Get-Content "$b\tv.txt") -join "`n"
$ok = $true
foreach ($r in $rates) {
    $h = @{}
    foreach ($e in $exes) {
        $m = [regex]::Match($txt, "(?s)$e $r\s*\r?\n.*?hash=([0-9A-F]{8})")
        $h[$e] = if ($m.Success) { $m.Groups[1].Value } else { '??' }
    }
    $same = ($h['gen16'] -eq $h['gen16o2']) -and ($h['gen16'] -eq $h['gen16nf']) -and ($h['gen16'] -ne '??')
    if (-not $same) { $ok = $false }
    '{0,6} Hz  full={1}  opl2={2}  opl2+nofp={3}  {4}' -f $r, $h['gen16'], $h['gen16o2'], $h['gen16nf'], $(if ($same) { 'IDENTICAL' } else { 'MISMATCH' })
}
if ($ok) { 'ALL VARIANTS BIT-IDENTICAL' } else { 'MISMATCH FOUND'; exit 1 }
