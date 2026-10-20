<#
    Report what Windows and what libusb each make of an attached Zune.

    Read-only: writes nothing, installs nothing, needs no elevation.  Run it
    before and after a driver install, and paste the whole output into a report.
    packaging/windows/DRIVER_INSTALL.md is the install guide it measures against.

      powershell -NoProfile -File packaging\windows\check-driver.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-Prop {
    param([string]$Id, [string]$Key)
    try { return (Get-PnpDeviceProperty -InstanceId $Id -Key $Key -ErrorAction Stop).Data }
    catch { return $null }
}

$nodes = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like 'USB\VID_045E*' })

if ($nodes.Count -eq 0) {
    Write-Host 'devnodes: none.  Nothing with USB\VID_045E is attached right now.'
    Write-Host 'Attach the player and run this again - it is the first gate.'
    exit 0
}

foreach ($node in $nodes) {
    $id = $node.InstanceId
    Write-Host ('devnode  : {0}' -f $id)
    Write-Host ('status   : {0}  problem={1}' -f $node.Status, $node.Problem)
    Write-Host ('hwids    : {0}' -f ((Get-Prop $id 'DEVPKEY_Device_HardwareIds') -join ' '))
    Write-Host ('compat   : {0}' -f ((Get-Prop $id 'DEVPKEY_Device_CompatibleIds') -join ' '))
    Write-Host ('driver   : {0}' -f (Get-Prop $id 'DEVPKEY_Device_Driver'))
    $service = Get-Prop $id 'DEVPKEY_Device_DriverService'
    if ($service) { Write-Host ('service  : {0}' -f $service) }
    $children = Get-Prop $id 'DEVPKEY_Device_Children'
    Write-Host ('children : {0}' -f $(if ($children) { $children -join ' ' } else { 'none - single function' }))
    $guids = Get-Prop $id 'DEVPKEY_Device_DeviceInterfaceGuids'
    if ($guids) {
        Write-Host ('ifaces   : {0}' -f ($guids -join ' '))
    } else {
        Write-Host 'ifaces   : none registered.  libusb reads DeviceInterfaceGUID(s)'
        Write-Host '           and returns LIBUSB_ERROR_ACCESS without one, so an'
        Write-Host '           openable player cannot be expected yet.'
    }
    Write-Host ''
}

Write-Host '== what the app sees =='
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tool = Join-Path $repo 'build\devicetool.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    Write-Host 'build\devicetool.exe is not built, so the libusb side is unknown.'
    exit 0
}
& $tool | ForEach-Object { Write-Host ('  {0}' -f $_) }
Write-Host ''
Write-Host 'agree means the platform agrees a reachable Zune is attached.  devicetool'
Write-Host 'opens no handle, so it says nothing about breach or MTPZ auth - that is'
Write-Host 'what launching build\zuuned.exe is for.'
