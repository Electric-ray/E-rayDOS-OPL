# Clones the upstream reference sources into ref/ at the revisions this project was developed against.
# ref/ is NOT committed; the build scripts patch copies of these sources at build time and never modify them.
# Required for the main build: VSBHDA (DBOPL).  Optional: ymfm (early prototypes), adlcom (reading the original TSR / QPI code).
param([switch]$All)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$repos = @(
    @{ name = 'VSBHDA'; url = 'https://github.com/Baron-von-Riedesel/VSBHDA.git'; rev = '756d41838bd4'; need = $true },
    @{ name = 'ymfm';   url = 'https://github.com/aaronsgiles/ymfm.git';            rev = '81aec25ccbb9'; need = $false },
    @{ name = 'adlcom'; url = 'https://github.com/Electric-ray/adlcom.git';         rev = '2e1c51cc8bea'; need = $false },
    @{ name = 'hearo';  url = 'https://github.com/tonyuatkins-afk/hearo.git';       rev = 'ac39f450d765'; need = $false },
    @{ name = 'opal';   url = 'https://github.com/RealBitdancer/opal.git';          rev = '5903c7e06783'; need = $false },
    @{ name = 'SBEMU';  url = 'https://github.com/crazii/SBEMU.git';                rev = '82c2d66b5d45'; need = $false },
    @{ name = 'E-RayDSB'; url = 'https://github.com/Electric-ray/E-RayDSB.git';     rev = '35e4b05d993c'; need = $false }
)
New-Item -ItemType Directory -Force -Path "$root\ref" | Out-Null
foreach ($r in $repos) {
    if (-not ($r.need -or $All)) { continue }
    $dir = "$root\ref\$($r.name)"
    if (-not (Test-Path "$dir\.git")) { git clone $r.url $dir }
    git -C $dir fetch --quiet origin
    git -C $dir checkout --quiet $r.rev
    Write-Host ("{0,-10} {1}" -f $r.name, (git -C $dir rev-parse --short=12 HEAD))
}
Write-Host 'done. (use -All to fetch the optional reference repositories too)'
