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
    $nodeKey = 'HKLM:\SYSTEM\CurrentControlSet\Enum\{0}' -f $id
    $service = Get-Prop $id 'DEVPKEY_Device_DriverService'
    if (-not $service) {
        try { $service = (Get-ItemProperty -LiteralPath $nodeKey -ErrorAction Stop).Service }
        catch { $service = $null }
    }
    if ($service) { Write-Host ('service  : {0}' -f $service) }
    $children = Get-Prop $id 'DEVPKEY_Device_Children'
    Write-Host ('children : {0}' -f $(if ($children) { $children -join ' ' } else { 'none - single function' }))
    $guids = @(Get-Prop $id 'DEVPKEY_Device_DeviceInterfaceGuids' | Where-Object { $_ })
    $source = 'DEVPKEY'
    if (-not $guids) {
        # Measured on a bound Zune HD: this DEVPKEY came back empty while the
        # devnode's Device Parameters key carried the GUID libusb opens with, so
        # the DEVPKEY alone must not be read as "no interface registered".
        $dp = Join-Path $nodeKey 'Device Parameters'
        try {
            $p = Get-ItemProperty -LiteralPath $dp -ErrorAction Stop
            $found = @($p.DeviceInterfaceGUIDs) + @($p.DeviceInterfaceGUID)
            $guids = @($found | Where-Object { $_ })
            if ($guids) { $source = 'registry Device Parameters' }
        } catch { $guids = $null }
    }
    if ($guids) {
        $list = ($guids | Select-Object -Unique) -join ' '
        Write-Host ('ifaces   : {0}  [{1}]' -f $list, $source)
    } else {
        Write-Host 'ifaces   : none in DEVPKEY and none in Device Parameters.  libusb'
        Write-Host '           reads DeviceInterfaceGUID(s) and returns'
        Write-Host '           LIBUSB_ERROR_ACCESS without one, so an openable player'
        Write-Host '           cannot be expected yet.'
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
