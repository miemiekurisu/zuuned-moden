# Compile the Chinese catalogue from the .ts into a .qm - no C++ build involved.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File i18n\build_qm.ps1 [-Fill] [-NextToExe]
#
# -Fill runs i18n/fill.py --write first, which is how the Chinese written by hand
# in i18n/zh_CN.tsv gets into the .ts.  The .qm lands in i18n\build\, and with
# -NextToExe a copy goes to build\i18n\ beside build\zuuned.exe, where
# installUiLanguage() prefers it over the catalogue compiled into the binary.
# That is the whole loop for rewording the interface as data:
#
#   edit i18n\zh_CN.tsv -> this script with -Fill -NextToExe -> restart zuuned
#
# lrelease prints the count it finished, which is the coverage number to quote.
# Do not commit a .qm; i18n/zh_CN.tsv and the .ts are the checked-in artefacts.
param(
    [string]$Lrelease = "D:\dev\Qt\6.11.0\msvc2022_64\bin\lrelease.exe",
    [string]$ExeDir = "build",
    [switch]$Fill,
    [switch]$NextToExe
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath $Lrelease)) {
    throw "lrelease not found at $Lrelease; pass -Lrelease <path> for another Qt."
}
Push-Location $root
try {
    if ($Fill) {
        python i18n/fill.py --write
        if ($LASTEXITCODE -ne 0) { throw "fill.py failed with $LASTEXITCODE" }
    }
    $ts = Join-Path $PSScriptRoot "zuuned_zh_CN.ts"
    $outDir = Join-Path $PSScriptRoot "build"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $qm = Join-Path $outDir "zuuned_zh_CN.qm"
    & $Lrelease $ts -qm $qm
    if ($LASTEXITCODE -ne 0) { throw "lrelease failed with $LASTEXITCODE" }
    "qm=$qm bytes=" + (Get-Item -LiteralPath $qm).Length
    if ($NextToExe) {
        $dstDir = Join-Path $root (Join-Path $ExeDir "i18n")
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
        Copy-Item -LiteralPath $qm -Destination $dstDir -Force
        "installed=" + (Join-Path $dstDir "zuuned_zh_CN.qm")
    }
}
finally { Pop-Location }
