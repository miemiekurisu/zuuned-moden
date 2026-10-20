# Windows usability gate for Zuuned.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\win-usability\run.ps1
#
# Answers one question with evidence: is this Windows build actually usable?  It
# is the GUI equivalent of tests/win-compat: no device, no sound (ZUUNED_MPV_AO is
# the null driver), no network requirement and no contact with the real profile,
# because every launch gets its own ZUUNED_PROFILE_DIR plus its own
# ZUUNED_LOCK_SUFFIX and the run compares the real profile before and after.
#
# A started process is not proof of anything on Windows: a Qt Quick window can
# come up with a clean stderr and paint nothing.  So each case ends with a
# winshot capture of the live window, and the app itself reports what it decided
# to show through `--qa-probe`, which is what lets the gate say that --page took
# effect instead of inferring it from a screenshot.
#
# Needs a built build\zuuned.exe (and librarytool, transcodetool, playertool) and
# the MSVC x64 environment for winshot, loaded from msvc-env.ps1 when cl is not
# already on PATH.  No Zune has ever been connected to a Windows build and nothing
# here touches USB: this certifies the UI, the tools and the profile, not the wire.
param(
    [string]$Deps = 'D:\dev\zuuned-deps',
    [string]$QtDir = 'D:\dev\Qt\6.11.0\msvc2022_64',
    [string]$FfmpegDir = 'D:\dev\zuuned-deps\ffmpeg-master-latest-win64-gpl-shared\bin',
    [int]$Settle = 6,
    [int]$CaseTimeout = 180,
    [string]$Only = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build'
$out = Join-Path $build 'win-usability'
$profiles = Join-Path $out 'profiles'
$shots = Join-Path $out 'shots'
$fixture = Join-Path $out 'fixture'
$exe = Join-Path $build 'zuuned.exe'
$libraryTool = Join-Path $build 'librarytool.exe'
$transcodeTool = Join-Path $build 'transcodetool.exe'
$playerTool = Join-Path $build 'playertool.exe'
$winshot = Join-Path $out 'winshot.exe'
$python = (Get-Command python -ErrorAction SilentlyContinue)

foreach ($d in @($out, $profiles, $shots, $fixture)) {
    if (Test-Path $d) { python -c "import shutil,sys;shutil.rmtree(sys.argv[1])" $d }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    . (Join-Path $Deps 'msvc-env.ps1')
}

$script:checks = 0
$script:failures = 0
$script:skips = 0
$script:failed = New-Object System.Collections.Generic.List[string]
$script:noise = New-Object System.Collections.Generic.List[string]
$script:caseSeq = 0
$script:pidScans = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    $script:checks++
    if ($Condition) {
        Write-Host ('  PASS ' + $Name)
    }
    else {
        $script:failures++
        $script:failed.Add($Name)
        Write-Host ('  FAIL ' + $Name + $(if ($Detail) { '  :: ' + $Detail } else { '' }))
    }
}

function Skip {
    param([string]$Name, [string]$Why)
    $script:checks++
    $script:skips++
    Write-Host ('  SKIP ' + $Name + '  ::  ' + $Why)
}

function Select-Phase {
    param([string]$Name)
    return (-not $Only) -or ($Name -like ('*' + $Only + '*'))
}

# Quote every argument so a path with a space or a bracket survives the MSVC CRT
# argument parser untouched.  Trailing backslashes are dropped because inside
# quotes they would escape the closing quote.
function Format-Args {
    param([string[]]$Arguments)
    return ($Arguments | ForEach-Object {
        $a = $_
        while ($a.EndsWith('\')) { $a = $a.Substring(0, $a.Length - 1) }
        '"' + $a + '"'
    }) -join ' '
}

function Invoke-Proc {
    param(
        [string]$File,
        [string[]]$Arguments = @(),
        [string]$WorkDir = $build,
        [hashtable]$Env = @{},
        [int]$Timeout = 120
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $File
    $psi.Arguments = Format-Args -Arguments $Arguments
    $psi.WorkingDirectory = $WorkDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $psi.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }
    $p = [System.Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($Timeout * 1000)) {
        try { $p.Kill($true) } catch { $p.Kill() }
        return [pscustomobject]@{ Code = 124; Out = ''; Err = 'timed out after ' + $Timeout + 's'; TimedOut = $true }
    }
    $p.WaitForExit()
    return [pscustomobject]@{
        Code = $p.ExitCode
        Out = $outTask.Result
        Err = $errTask.Result
        TimedOut = $false
    }
}

$script:hwndU32 = { param($handle) [uint32]([int32]$handle.ToInt64() -band 0xFFFFFFFF) }

function ConvertTo-HwndDecimal {
    param([IntPtr]$Handle)
    # MainWindowHandle hands back a signed 32-bit value for handles above
    # 0x7FFFFFFF; winshot parses unsigned decimal, so widen before printing.
    return ([uint32]([int]($Handle.ToInt64() -band 0xFFFFFFFFL))).ToString()
}

# ---------------------------------------------------------------------------
# launch plumbing
# ---------------------------------------------------------------------------

function Start-Zuuned {
    param([string]$Profile, [string]$Suffix, [string[]]$Arguments)
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.Arguments = Format-Args -Arguments $Arguments
    $psi.WorkingDirectory = $build
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $psi.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $psi.EnvironmentVariables['ZUUNED_PROFILE_DIR'] = $Profile
    $psi.EnvironmentVariables['ZUUNED_LOCK_SUFFIX'] = $Suffix
    $psi.EnvironmentVariables['ZUUNED_MPV_AO'] = 'null'
    $psi.EnvironmentVariables['PATH'] = (Join-Path $QtDir 'bin') + ';' + $FfmpegDir + ';' + $env:PATH
    $p = [System.Diagnostics.Process]::Start($psi)
    return [pscustomobject]@{
        Proc = $p
        OutTask = $p.StandardOutput.ReadToEndAsync()
        ErrTask = $p.StandardError.ReadToEndAsync()
        Profile = $Profile
        Suffix = $Suffix
        Killed = $false
    }
}

function Resolve-ZuunedWindowByPid {
    # Win32 caches nothing, so this is the ground truth for "which visible window
    # belongs to this process id".
    param([int]$ProcessId)
    $r = Invoke-Proc -File $winshot -Arguments @('--pid', [string]$ProcessId) -WorkDir $out -Timeout 20
    if ($r.Code -ne 0) { return [IntPtr]::Zero }
    $m = [regex]::Match($r.Out, 'hwnd=(\d+)')
    if (-not $m.Success) { return [IntPtr]::Zero }
    return [IntPtr][long]::Parse($m.Groups[1].Value)
}

function Wait-ZuunedWindow {
    # PowerShell 7 reads Process.MainWindowHandle once and then keeps the answer, so
    # a harness that looks at it while the splash is still mapping reports an empty
    # handle for a window the user can plainly see: one probe found no handle on 30
    # consecutive healthy launches.  Refresh() drops that cache and the pid scan is
    # the check that cannot lie, and the case records which one answered because a
    # screenshot of a handle nobody can explain is not evidence.
    param($Handle, [int]$Seconds = 30)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ($true) {
        if ($Handle.Proc.HasExited) {
            return [pscustomobject]@{ Handle = [IntPtr]::Zero; Source = 'exited' }
        }
        $Handle.Proc.Refresh()
        $h = $Handle.Proc.MainWindowHandle
        if ($h -ne [IntPtr]::Zero) {
            return [pscustomobject]@{ Handle = $h; Source = 'property' }
        }
        $h = Resolve-ZuunedWindowByPid -ProcessId $Handle.Proc.Id
        if ($h -ne [IntPtr]::Zero) {
            $script:pidScans++
            return [pscustomobject]@{ Handle = $h; Source = 'pid-scan' }
        }
        if ((Get-Date) -ge $deadline) {
            return [pscustomobject]@{ Handle = [IntPtr]::Zero; Source = 'none' }
        }
        Start-Sleep -Milliseconds 250
    }
}

function Stop-Zuuned {
    # Closing the window is the shutdown path a user takes; Kill is recorded as a
    # defect because it means the app would not come down politely.
    param($Handle, [int]$Seconds = 30)
    $p = $Handle.Proc
    if (-not $p.HasExited) {
        [void]$p.CloseMainWindow()
        if (-not $p.WaitForExit($Seconds * 1000)) {
            $Handle.Killed = $true
            try { $p.Kill($true) } catch { $p.Kill() }
            [void]$p.WaitForExit(5000)
        }
    }
    $text = ($Handle.ErrTask.Result -split "`r?`n") + ($Handle.OutTask.Result -split "`r?`n")
    $code = $p.ExitCode
    if ($Handle.Killed) {
        # A destroyed process reports an exit code it never chose, so 124 is the
        # gate's own: a forced kill can never be read as a clean shutdown.
        $code = 124
    }
    return [pscustomobject]@{
        Code = $code
        Killed = [bool]$Handle.Killed
        Lines = @($text | Where-Object { $_ -ne '' })
    }
}

$script:noisePattern =
    'qml:|TypeError|ReferenceError|Cannot read|is not defined|does not exist|Local logs unavailable|did not capture|profile isolation is incomplete'

function Find-Noise {
    param([string[]]$Lines)
    return @($Lines | Where-Object { $_ -match $script:noisePattern -and $_ -notmatch '\[qa\] ' })
}

function Note-Diagnostics {
    # Recorded, not asserted: warning lines worth reading once the hard failures
    # are gone, without letting a benign one turn the gate red.
    param([string]$Case, [string[]]$Lines)
    foreach ($line in $Lines) {
        if ($line -match '(?i)(warn|error|fail)' -and $line -notmatch $script:noisePattern -and $line -notmatch '\[qa\] ') {
            $script:noise.Add($Case + ' | ' + $line.Trim())
        }
    }
}

function Read-Qa {
    # The app's own account of what it is showing, newest first.
    param([string[]]$Lines, [string]$Selector = '')
    $matches2 = @($Lines | Where-Object { $_ -match '\[qa\] ' })
    if ($Selector) { $matches2 = @($matches2 | Where-Object { $_ -match $Selector }) }
    if ($matches2.Count -eq 0) { return $null }
    $line = [string]$matches2[-1]
    $table = @{}
    foreach ($m in [regex]::Matches($line, '([a-zA-Z]+)=([^\s]+)')) {
        $table[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    $table['line'] = $line
    return $table
}

function Invoke-Capture {
    param([string]$Hwnd, [string]$Png)
    $r = Invoke-Proc -File $winshot -Arguments @($Hwnd, $Png) -WorkDir $out -Timeout 60
    $parsed = @{}
    foreach ($m in [regex]::Matches($r.Out, '([a-z]+)=([^\s]+)')) {
        $parsed[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    $parsed['Code'] = $r.Code
    $parsed['Png'] = $Png
    $parsed['Out'] = $r.Out.Trim()
    return $parsed
}

function Get-PaintVerdict {
    # A painted Zuuned page measures about 9,500 distinct colours with the most
    # common one near 61% of the frame; a window that never presented measures
    # one colour.  The thresholds stay far away from both.
    param($Shot)
    $distinct = 0
    $dominant = 0.0
    $mean = 0.0
    [void][int]::TryParse([string]$Shot['distinct'], [ref]$distinct)
    [void][double]::TryParse([string]$Shot['dominant'], [ref]$dominant)
    [void][double]::TryParse([string]$Shot['mean'], [ref]$mean)
    return [pscustomobject]@{
        Painted = ($Shot.Code -eq 0 -and $distinct -ge 300 -and $dominant -lt 0.97 -and $mean -ge 2)
        Distinct = $distinct
        Dominant = $dominant
        Mean = $mean
    }
}

function Invoke-Case {
    <#
      One instrumented launch: its own profile folder, its own instance lock, the
      null audio driver, a settle long enough to clear the two-second splash, a
      capture of what it actually painted, and a polite window close.  $Drive runs
      while the window is up for cases that have to change something first (a
      resize, a second capture).
    #>
    param(
        [string]$Name,
        [string[]]$Arguments = @(),
        [switch]$Seed,
        [scriptblock]$Drive
    )
    $script:caseSeq++
    $tag = $script:caseSeq.ToString('00') + '-' + $Name
    $profile = Join-Path $profiles $tag
    New-Item -ItemType Directory -Force -Path $profile | Out-Null
    if ($Seed) {
        Copy-Item -LiteralPath (Join-Path $out 'seed-library.db') -Destination (Join-Path $profile 'library.db')
    }
    $handle = Start-Zuuned -Profile $profile -Suffix ('wus' + $script:caseSeq) `
        -Arguments (@($Arguments) + @('--qa-probe'))
    $window = Wait-ZuunedWindow -Handle $handle
    $hwnd = $window.Handle
    $shot = $null
    $verdict = $null
    $extra = $null
    if ($hwnd -ne [IntPtr]::Zero) {
        Start-Sleep -Seconds $Settle
        $shot = Invoke-Capture -Hwnd (ConvertTo-HwndDecimal -Handle $hwnd) -Png (Join-Path $out ('shots\' + $tag + '.png'))
        $verdict = Get-PaintVerdict -Shot $shot
        if ($Drive) { $extra = & $Drive $handle }
    }
    $stop = Stop-Zuuned -Handle $handle
    Note-Diagnostics -Case $tag -Lines $stop.Lines
    return [pscustomobject]@{
        Name = $Name
        Tag = $tag
        Profile = $profile
        WindowSeen = ($hwnd -ne [IntPtr]::Zero)
        Hwnd = $hwnd
        HwndSource = $window.Source
        Shot = $shot
        Verdict = $verdict
        Extra = $extra
        Exit = $stop.Code
        Killed = $stop.Killed
        Lines = $stop.Lines
        Noise = @(Find-Noise -Lines $stop.Lines)
        QaSplash = Read-Qa -Lines $stop.Lines -Selector '\[qa\] splash '
        QaLast = Read-Qa -Lines $stop.Lines
    }
}

function Assert-Case {
    param($Case, [string]$Label, [string]$ExpectPage = '', [string]$ExpectOnboarding = '')
    $problems = @()
    if (-not $Case.WindowSeen) { $problems += 'no window appeared' }
    if ($Case.Killed) { $problems += 'did not close politely' }
    if ($Case.Exit -ne 0) { $problems += ('exit=' + $Case.Exit) }
    if ($Case.Noise.Count -gt 0) { $problems += ('stderr: ' + $Case.Noise[0].Trim()) }
    if (-not $Case.Verdict) { $problems += 'no capture' }
    elseif (-not $Case.Verdict.Painted) {
        $problems += ('blank-ish distinct=' + $Case.Verdict.Distinct + ' dominant=' + ('{0:N3}' -f $Case.Verdict.Dominant))
    }
    $qa = $Case.QaSplash
    if (-not $qa) { $problems += 'no [qa] splash report' }
    else {
        if ($ExpectPage -and $qa['page'] -ne $ExpectPage) { $problems += ('page=' + $qa['page']) }
        if ($ExpectOnboarding -and $qa['onboarding'] -ne $ExpectOnboarding) {
            $problems += ('onboarding=' + $qa['onboarding'])
        }
    }
    Check -Name $Label -Condition ($problems.Count -eq 0) -Detail ($problems -join '; ')
}

function Get-RealProfileFingerprint {
    # The point of the whole run: an instrumented launch must not move a byte of
    # the profile the user actually uses.
    $count = 0
    $bytes = 0
    $newest = 0
    foreach ($target in @((Join-Path $env:APPDATA 'Zuuned'), (Join-Path $env:LOCALAPPDATA 'Zuuned'))) {
        if (-not (Test-Path -LiteralPath $target)) { continue }
        Get-ChildItem -LiteralPath $target -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object {
            $count++
            $bytes += $_.Length
            if ($_.LastWriteTimeUtc.Ticks -gt $newest) { $newest = $_.LastWriteTimeUtc.Ticks }
        }
    }
    return ($count.ToString() + ' files / ' + $bytes + ' bytes / newest ' + $newest)
}

$ffmpeg = Join-Path $FfmpegDir 'ffmpeg.exe'
$fixtureMusic = Join-Path $fixture 'music'
$deepLeaf = ('segment-' * 24) + 'leaf'
$seedDb = Join-Path $out 'seed-library.db'

Write-Host '== capture tool'
if (Select-Phase 'winshot') {
    $r = Invoke-Proc -File 'cl.exe' -Arguments @(
        '/nologo', '/W3', '/Od', '/Zi', '/MD', '/EHsc', '/std:c++17', '/DUNICODE', '/D_UNICODE',
        (Join-Path $root 'tests\win-usability\winshot.cpp'),
        ('/Fe:' + $winshot),
        ('/Fo:' + (Join-Path $out 'winshot.obj')),
        ('/Fd:' + (Join-Path $out 'winshot.pdb')),
        '/link', 'user32.lib', 'gdi32.lib', 'gdiplus.lib'
    ) -WorkDir $root -Timeout 240
    Check 'winshot compiles' ($r.Code -eq 0 -and (Test-Path $winshot)) (($r.Out + $r.Err).Trim())
    Check 'winshot build leaves no incremental pdb in the checkout' `
        (-not (Test-Path (Join-Path $root 'vc140.pdb')))
}
else {
    if (-not (Test-Path $winshot)) { throw 'winshot.exe is missing and this run was filtered away from building it' }
}

Write-Host ''
Write-Host '== gate coverage'
# What follows is deliberately not a pass.  The cases below are planned and not
# implemented yet; naming them here and leaving a non-zero exit code is what stops
# a partial run from being quoted as a passed Windows usability gate.
$Planned = @(
    'one launch case per --page, after dismissing onboarding',
    'the five settings tabs',
    'wide and compact chrome, driven by winshot --move',
    'the CJK and long-path librarytool scan fixture',
    'transcodetool probe',
    'a silent playertool seek',
    'single-instance refusal',
    'session-log creation and settings persistence',
    'the real profile unchanged before and after the run'
)
foreach ($p in $Planned) { Write-Host ('  TODO ' + $p) }
Write-Host ('checks=' + $script:checks + ' failures=' + $script:failures + ' skips=' + $script:skips)
if ($script:failures) {
    foreach ($f in $script:failed) { Write-Host ('  FAILED ' + $f) }
    Write-Host 'OVERALL=fail'
    exit 1
}
Write-Host 'OVERALL=incomplete only the capture-tool phase is implemented'
exit 2
