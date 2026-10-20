# Windows usability gate for Zuuned.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\win-usability\run.ps1
#
# Answers one question with evidence: is this Windows build actually usable?  It
# is the GUI equivalent of tests/win-compat: no device, no sound (ZUUNED_MPV_AO is
# the null driver) and no contact with the real profile, because every launch gets
# its own ZUUNED_PROFILE_DIR plus its own ZUUNED_LOCK_SUFFIX and the run compares
# the real profile before and after.  It needs no network to pass, but a launch
# whose seeded library lacks covers does reach musicbrainz.org; that traffic is
# incidental, logged per launch, and never asserted for or against.
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
    [string[]]$Only = @()
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
$script:killed = 0
$script:pids = New-Object System.Collections.Generic.List[int]

# A harness that dies halfway must not leave an instrumented Zuuned holding a
# profile folder and an instance lock that the next run intends to delete: one
# broken helper once aborted this script mid-case and left a launched app behind.
trap {
    Write-Host ('harness stopped: ' + $_.Exception.Message)
    foreach ($orphan in @($script:pids)) {
        $proc = Get-Process -Id $orphan -ErrorAction SilentlyContinue
        if (-not $proc -or $proc.HasExited) { continue }
        [void]$proc.CloseMainWindow()
        if (-not $proc.WaitForExit(10000)) {
            try { $proc.Kill($true) } catch { $proc.Kill() }
        }
    }
    Write-Host 'OVERALL=fail'
    exit 1
}

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
    # -Only takes a list so a filtered run can pair a phase with the fixture it
    # needs: the scratch directory is wiped per invocation, so nothing carries over.
    param([string]$Name)
    if (-not $Only) { return $true }
    foreach ($pattern in ($Only -join ',' -split ',')) {
        $pattern = $pattern.Trim()
        if (-not $pattern) { continue }
        if ($Name -like ('*' + $pattern + '*')) { return $true }
    }
    return $false
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
    # A handle above 0x7FFFFFFF reaches this harness by two routes and they are not
    # the same number: the .NET property sign-extends the 32-bit value, so it is
    # negative, while `winshot --pid` prints unsigned decimal and
    # Resolve-ZuunedWindowByPid widens that straight into an IntPtr.  Mask to 32
    # bits, which is what both of them mean, and print unsigned because that is what
    # winshot parses.  An earlier version of this line cast through [int] as well,
    # which throws on precisely the large handles this is here for - and it only
    # showed up on a run that needed the pid scan, because the property route hands
    # back a negative value that the cast survived.
    return (($Handle.ToInt64() -band 0xFFFFFFFFL).ToString())
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

function Read-QaAll {
    # Every report the application made, in order.  Read-Qa answers "what did it
    # end up showing"; a case that drives a resize needs "did it pass through the
    # state that resize is supposed to cause", and that state only ever appears in
    # the middle of the log.
    param([string[]]$Lines)
    $table = @()
    foreach ($line in @($Lines | Where-Object { $_ -match '\[qa\] ' })) {
        $entry = @{}
        foreach ($m in [regex]::Matches([string]$line, '([a-zA-Z]+)=([^\s]+)')) {
            $entry[$m.Groups[1].Value] = $m.Groups[2].Value
        }
        $entry['line'] = [string]$line
        $table += $entry
    }
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

function Format-KvDetail {
    # Keep a failure detail readable in a console whose code page is not UTF-8: the
    # python companions print the CJK fixture only on the escaped paths= and titles=
    # lines, which are exactly the lines worth leaving out of a one-line detail.
    param([string]$Out)
    return ((($Out -split "`r?`n") | Where-Object {
        $_ -match '^[a-z_]+=' -and $_ -notmatch '^(paths|titles)='
    }) -join ' ')
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
    $script:pids.Add($handle.Proc.Id)
    $window = Wait-ZuunedWindow -Handle $handle
    $hwnd = $window.Handle
    $shot = $null
    $verdict = $null
    $extra = $null
    if ($hwnd -ne [IntPtr]::Zero) {
        Start-Sleep -Seconds $Settle
        $shot = Invoke-Capture -Hwnd (ConvertTo-HwndDecimal -Handle $hwnd) -Png (Join-Path $out ('shots\' + $tag + '.png'))
        $verdict = Get-PaintVerdict -Shot $shot
        if ($Drive) { $extra = & $Drive $handle $hwnd $tag }
    }
    $stop = Stop-Zuuned -Handle $handle
    if ($stop.Killed) { $script:killed++ }
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

function Get-RealProfileFiles {
    # The profile the user actually uses, as files.  A byte-for-byte compare would
    # blame this gate for the daily Zuuned checkpointing its own library.db-wal, so
    # the gate asserts attribution instead: nothing it started may show up here.
    param([DateTime]$Since = [DateTime]::MinValue)
    $found = @()
    foreach ($target in @((Join-Path $env:APPDATA 'Zuuned'), (Join-Path $env:LOCALAPPDATA 'Zuuned'))) {
        if (-not (Test-Path -LiteralPath $target)) { continue }
        $found += Get-ChildItem -LiteralPath $target -Recurse -File -Force -ErrorAction SilentlyContinue
    }
    return @($found | Where-Object { $_.CreationTimeUtc -ge $Since })
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
$seedDb = Join-Path $out 'seed-library.db'
$tone = Join-Path $fixtureMusic 'Tone A.mp3'
$pythonExe = ''
if ($python) { $pythonExe = $python.Source }
$script:runStart = (Get-Date).ToUniversalTime()
$beforeFp = Get-RealProfileFingerprint

Write-Host '== capture tool'
# The run wipes its own scratch directory, so the capture tool is rebuilt on
# every invocation, filtered or not: every later phase photographs through it.
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

Write-Host ''
Write-Host '== scan fixture: CJK album folder, path past MAX_PATH, WMA named .mp3'
if (Select-Phase 'fixture') {
    if (-not $pythonExe) {
        Skip 'the scan fixture phase' 'python is not on PATH'
    }
    else {
        $fx = Invoke-Proc -File $pythonExe -Arguments @(
            (Join-Path $PSScriptRoot 'make_scan_fixture.py'), $fixtureMusic,
            '--ffmpeg', $ffmpeg, '--force') -WorkDir $out -Timeout 300
        Check 'the fixture put four tracks on disk' `
            ($fx.Code -eq 0 -and $fx.Out -match 'created=4') `
            (Format-KvDetail -Out ($fx.Out + [Environment]::NewLine + $fx.Err))

        $scanDb = Join-Path $out 'scan-library.db'
        $scan = Invoke-Proc -File $libraryTool -Arguments @('scan', $fixtureMusic, $scanDb) -Timeout 900
        Check 'librarytool scan returns 0 over the CJK and deep-path tree' `
            ($scan.Code -eq 0) ('code=' + $scan.Code)
        Check 'librarytool scan counted four tracks' `
            ($scan.Out -match '4 tracks added') 'no added-track line in the captured output'

        $insp = Invoke-Proc -File $pythonExe -Arguments @(
            (Join-Path $PSScriptRoot 'inspect_scan_db.py'), $scanDb,
            '--expect-tracks', '4', '--expect-codepoints', '0x6F22,0x8A5E',
            '--min-longest-path', '260') -WorkDir $out -Timeout 180
        Check 'the scan stored the Chinese album folder and a path past 260 characters' `
            ($insp.Code -eq 0) (Format-KvDetail -Out $insp.Out)
        Check 'every scanned track was probed and left copy evidence' `
            ($insp.Out -match 'probe_failures=0' -and $insp.Out -match 'fingerprints=4' `
                -and $insp.Out -match 'nonzero_durations=yes') (Format-KvDetail -Out $insp.Out)
        if ($insp.Code -eq 0) {
            Copy-Item -LiteralPath $scanDb -Destination $seedDb -Force
        }
    }
}

$seeded = Test-Path -LiteralPath $seedDb

function Get-QaField {
    # A missing report has to read as a failed check, not as an index error in the
    # middle of a phase.
    param($Entry, [string]$Key)
    if ($null -eq $Entry) { return '' }
    return [string]$Entry[$Key]
}

function Get-QaSize {
    # 'size=1384x1181' as numbers.  The breakpoints the application uses are logical
    # pixels, so the gate compares what the window itself reported instead of what
    # winshot asked for: a minimum window size can move the second number.
    param($Entry)
    $w = 0
    $h = 0
    $m = [regex]::Match((Get-QaField $Entry 'size'), '^(\d+)x(\d+)$')
    if ($m.Success) {
        $w = [int]$m.Groups[1].Value
        $h = [int]$m.Groups[2].Value
    }
    return [pscustomobject]@{ W = $w; H = $h }
}

function Test-StatePath {
    # Subsequence match.  The QA reports are sampled on a timer rather than emitted
    # on every edge, so a resize shows up once or three times depending on how long
    # the drive dwelled in it; only the order of the states is meaningful.
    param([string[]]$States, [string[]]$Path)
    $at = 0
    foreach ($state in $States) {
        if ($state -eq $Path[$at]) {
            $at++
            if ($at -ge $Path.Count) { return $true }
        }
    }
    return $false
}

Write-Host ''
Write-Host '== pages'
if (Select-Phase 'pages') {
    if (-not $seeded) {
        Skip 'one launch case per --page' 'no seed library, so every case would only ever see onboarding'
    }
    else {
        foreach ($p in @('music', 'videos', 'photos', 'podcasts', 'audiobooks')) {
            $pageCase = Invoke-Case -Name ('page-' + $p) -Arguments @('--page', $p) -Seed
            Assert-Case -Case $pageCase -Label ('page ' + $p + ' paints and reports itself') `
                -ExpectPage $p -ExpectOnboarding 'false'
            if ($p -eq 'music') {
                Check 'the seeded library reaches the running application' `
                    ((Get-QaField $pageCase.QaSplash 'tracks') -eq '4') `
                    ('tracks=' + (Get-QaField $pageCase.QaSplash 'tracks'))
            }
        }
        # A device page with nothing attached is the case a user hits every time the
        # cable is out; it must open and stay quiet even though no device claim here
        # is possible.
        $deviceCase = Invoke-Case -Name 'page-deviceMusic' -Arguments @('--page', 'deviceMusic') -Seed
        Assert-Case -Case $deviceCase -Label 'a device page opens with no device attached and stays clean' `
            -ExpectOnboarding 'false'
        Write-Host ('  note device page reported page=' + (Get-QaField $deviceCase.QaSplash 'page') +
            ' panel=' + (Get-QaField $deviceCase.QaSplash 'panel'))
    }
}

Write-Host ''
Write-Host '== settings tabs'
if (Select-Phase 'settings') {
    if (-not $seeded) {
        Skip 'the five settings tabs' 'no seed library'
    }
    else {
        foreach ($tab in @('Appearance', 'Library', 'Device', 'Mixtapes', 'Health')) {
            $tabCase = Invoke-Case -Name ('tab-' + $tab) -Seed `
                -Arguments @('--page', 'settings', '--settings-tab', $tab)
            Assert-Case -Case $tabCase -Label ('settings tab ' + $tab + ' paints on the settings page') `
                -ExpectPage 'settings' -ExpectOnboarding 'false'
        }
    }
}

Write-Host ''
Write-Host '== chrome collapse pin'
if (Select-Phase 'collapse') {
    if (-not $seeded) {
        Skip 'both collapse states' 'no seed library'
    }
    else {
        foreach ($pin in @('0', '1')) {
            $pinCase = Invoke-Case -Name ('collapse-' + $pin) -Seed `
                -Arguments @('--page', 'music', '--collapse', $pin)
            Assert-Case -Case $pinCase -Label ('the header at collapse pin ' + $pin + ' paints') `
                -ExpectPage 'music' -ExpectOnboarding 'false'
            $want = ([double]$pin).ToString('0.00')
            Check ('the window reports collapse=' + $want) `
                ((Get-QaField $pinCase.QaSplash 'collapse') -eq $want) `
                ('collapse=' + (Get-QaField $pinCase.QaSplash 'collapse'))
        }
    }
}

Write-Host ''
Write-Host '== compact playback'
if (Select-Phase 'compact') {
    if (-not ($seeded -and (Test-Path -LiteralPath $tone))) {
        Skip 'the compact Now-Playing island' 'needs the seed library and the fixture tone'
    }
    else {
        # A default 1280x800 launch is already compact: the ribbon needs 1280 logical
        # pixels of width AND Theme.headroomCompactBelow (1100) of height, so the
        # useful question is not "does shrinking retire the ribbon" but whether the
        # window travels both ways across those breakpoints with a queue loaded.
        # The floor is Main.qml's own minimumWidth/Height (1100x700), still narrow
        # enough to retire the ribbon, and the drive asks for less than that on
        # purpose so the floor is what answers.
        $compactCase = Invoke-Case -Name 'compact' -Seed `
            -Arguments @('--page', 'music', '--qa-play', $tone) -Drive {
                param($Handle, $Hwnd, $Tag)
                $dec = ConvertTo-HwndDecimal -Handle $Hwnd
                $stages = @(
                    @{ Name = 'tall1'; W = 1400; H = 1220 },
                    @{ Name = 'narrow'; W = 900; H = 640 },
                    @{ Name = 'tall2'; W = 1400; H = 1220 }
                )
                $moves = @()
                $paint = @{}
                foreach ($stage in $stages) {
                    $moved = Invoke-Proc -File $winshot -Timeout 60 -WorkDir $out `
                        -Arguments @('--move', $dec, [string]$stage.W, [string]$stage.H)
                    $client = [regex]::Match($moved.Out, 'client=(\d+)x(\d+)')
                    $moves += [pscustomobject]@{
                        Stage = $stage.Name
                        Asked = ([string]$stage.W + 'x' + [string]$stage.H)
                        Client = $client.Groups[1].Value + 'x' + $client.Groups[2].Value
                    }
                    Start-Sleep -Seconds 5
                    $shot = Invoke-Capture -Hwnd $dec -Png (Join-Path $shots ($Tag + '-' + $stage.Name + '.png'))
                    $paint[$stage.Name] = Get-PaintVerdict -Shot $shot
                }
                return [pscustomobject]@{ Moves = $moves; Paint = $paint }
            }
        Assert-Case -Case $compactCase -Label 'a window with a queued track paints' `
            -ExpectPage 'music' -ExpectOnboarding 'false'
        $all = @(Read-QaAll -Lines $compactCase.Lines)
        $last = $null
        if ($all.Count) { $last = $all[$all.Count - 1] }
        Check 'the QA queue reached the player and the tone played' `
            (@($all | Where-Object {
                (Get-QaField $_ 'queue') -eq '1' -and (Get-QaField $_ 'playing') -eq 'true' `
                    -and (Get-QaField $_ 'dur') -eq '6000'
            }).Count -gt 0) ('reports=' + $all.Count)

        $states = @()
        foreach ($entry in $all) {
            $size = Get-QaSize -Entry $entry
            $roomy = ($size.W -ge 1280 -and $size.H -ge 1100)
            $retired = ((Get-QaField $entry 'ribbonRetired') -eq 'true')
            $tucked = ((Get-QaField $entry 'collapsed') -eq 'true')
            $name = 'compact'
            if ($roomy) { $name = 'roomy' }
            if ($retired) { $name += '-retired' } else { $name += '-ribbon' }
            $states += [pscustomobject]@{
                Name = $name
                Roomy = $roomy
                Retired = $retired
                Ribbon = (-not $retired)
                Tucked = $tucked
                Island = ((Get-QaField $entry 'nowPlaying') -eq 'true')
                Line = (Get-QaField $entry 'line')
            }
        }
        $sequence = @()
        foreach ($state in $states) {
            if ($sequence.Count -eq 0 -or $sequence[-1] -ne $state.Name) { $sequence += $state.Name }
        }
        Write-Host ('  note state sequence: ' + ($sequence -join ' -> '))
        foreach ($move in $compactCase.Extra.Moves) {
            Write-Host ('  note ' + $move.Stage + ' asked ' + $move.Asked + ' client ' + $move.Client)
        }

        # The trip the compact redesign is about, in the order the drive performed it.
        Check 'a compact window with a queue plays through the retired ribbon' `
            (Test-StatePath -States $sequence -Path @('compact-retired')) `
            ('sequence=' + ($sequence -join '>'))
        Check 'growing past 1280x1100 brings the top ribbon back' `
            (Test-StatePath -States $sequence -Path @('compact-retired', 'roomy-ribbon')) `
            ('sequence=' + ($sequence -join '>'))
        Check 'shrinking again retires the ribbon and the island is up' `
            (Test-StatePath -States $sequence -Path @('compact-retired', 'roomy-ribbon', 'compact-retired')) `
            ('sequence=' + ($sequence -join '>'))
        Check 'the ribbon survives a second grow' `
            (Test-StatePath -States $sequence `
                -Path @('compact-retired', 'roomy-ribbon', 'compact-retired', 'roomy-ribbon')) `
            ('sequence=' + ($sequence -join '>'))
        Check 'the header untucks with the ribbon and tucks again without it' `
            ((@($states | Where-Object { $_.Ribbon -and -not $_.Tucked }).Count -gt 0) -and
                (@($states | Where-Object { $_.Retired -and $_.Tucked }).Count -gt 0)) `
            ('ribbonStates=' + (@($states | Where-Object { $_.Ribbon }).Count))
        Check 'the window respected its own minimum size when squeezed' `
            (@($compactCase.Extra.Moves | Where-Object { $_.Stage -eq 'narrow' } | ForEach-Object {
                $parts = $_.Client -split 'x'
                ([int]$parts[0] -ge 1100) -and ([int]$parts[1] -ge 700)
            }).Count -eq 1) (($compactCase.Extra.Moves | ForEach-Object { $_.Client }) -join ' ')
        Check 'the roomy window painted' ($compactCase.Extra.Paint['tall2'].Painted) `
            ('distinct=' + $compactCase.Extra.Paint['tall2'].Distinct)
        Check 'the narrow window painted' ($compactCase.Extra.Paint['narrow'].Painted) `
            ('distinct=' + $compactCase.Extra.Paint['narrow'].Distinct)
        # Not asserted: the island answers its own close button, not a resize, so a
        # gate with no mouse cannot certify that closing it sticks.
        Write-Host ('  note after growing back: nowPlaying=' + (Get-QaField $last 'nowPlaying') +
            ' size=' + (Get-QaField $last 'size'))
    }
}

Write-Host ''
Write-Host '== command-line tools'
if (Select-Phase 'tools') {
    if (Test-Path -LiteralPath $tone) {
        $probe = Invoke-Proc -File $transcodeTool -Arguments @('probe', $tone) -Timeout 120
        Check 'transcodetool probes the MP3 fixture at six seconds' `
            ($probe.Code -eq 0 -and $probe.Out -match 'duration=6\.') ('out=' + $probe.Out.Trim())
        $sneaky = Invoke-Proc -File $transcodeTool -Timeout 120 `
            -Arguments @('probe', (Join-Path $fixtureMusic 'Sneaky Wma.mp3'))
        Check 'transcodetool sniffs the WMA stream inside a file named .mp3' `
            ($sneaky.Code -eq 0 -and $sneaky.Out -match 'duration=6\.') ('out=' + $sneaky.Out.Trim())
    }
    else {
        Skip 'transcodetool probe' 'no fixture tone on disk'
    }
    $names = Invoke-Proc -File $libraryTool -Arguments @('names') -Timeout 600
    Check 'librarytool names passes its 17-case naming self-test' `
        ($names.Code -eq 0 -and $names.Out -match 'ALL PASS \(0 failures\)') ('code=' + $names.Code)
    $cust = Invoke-Proc -File $libraryTool -Arguments @('customize') -Timeout 900
    Check 'librarytool customize runs the v0/v2 to v9 migration chain' `
        ($cust.Code -eq 0 -and $cust.Out -match 'ALL PASS \(0 failures\)') ('code=' + $cust.Code)
}

Write-Host ''
Write-Host '== player core'
if (Select-Phase 'seek') {
    if (-not (Test-Path -LiteralPath $tone)) {
        Skip 'the silent playertool checks' 'no fixture tone on disk'
    }
    else {
        $mpvEnv = @{ ZUUNED_MPV_AO = 'null' }
        $seeked = Invoke-Proc -File $playerTool -Arguments @('seek', $tone, '4') -Env $mpvEnv -Timeout 180
        Check 'playertool lands a seek made while stopped' `
            ($seeked.Code -eq 0 -and $seeked.Out -match 'PASS' -and $seeked.Out -notmatch 'FAIL') `
            $seeked.Out.Trim()
        $played = Invoke-Proc -File $playerTool -Arguments @('play', $tone, '2') -Env $mpvEnv -Timeout 180
        Check 'playertool position advances on the null audio driver' `
            ($played.Out -match 'monotonic=yes') $played.Out.Trim()
    }
}

Write-Host ''
Write-Host '== single instance'
if (Select-Phase 'single') {
    # The lock lives inside the profile, at <profile>/run/zuuned<SUFFIX>.lock, so the
    # only rival the app can refuse is one that names the same profile and the same
    # suffix.  Two profiles of the same build hold two locks and both are right to
    # run, which is what an earlier version of this phase asked for; it then waited
    # 120 s for a rival that was behaving correctly and called the app broken.
    $sharedDir = Join-Path $profiles 'single-instance'
    New-Item -ItemType Directory -Force -Path $sharedDir | Out-Null
    $holder = Start-Zuuned -Profile $sharedDir -Suffix 'wussingle' -Arguments @('--page', 'music')
    $script:pids.Add($holder.Proc.Id)
    $holderWindow = Wait-ZuunedWindow -Handle $holder
    Start-Sleep -Seconds 3
    $rival = Invoke-Proc -File $exe -WorkDir $build -Timeout 90 -Arguments @('--page', 'music') -Env @{
        ZUUNED_PROFILE_DIR = $sharedDir
        ZUUNED_LOCK_SUFFIX = 'wussingle'
        ZUUNED_MPV_AO = 'null'
        PATH = (Join-Path $QtDir 'bin') + ';' + $FfmpegDir + ';' + $env:PATH
    }
    Check 'a second instance on the same profile bows out in its own words' `
        ((($rival.Out + $rival.Err) -match 'another instance is already running') -and (-not $rival.TimedOut)) `
        ('code=' + $rival.Code + ' out=' + $rival.Out.Trim() + ' err=' + $rival.Err.Trim())
    # Bowing out happens before the QML engine exists, so a rival that reports what
    # page it is on has not bowed out at all - it has started a second app on a
    # library the holder is already using, which is the hazard C2 exists to stop.
    Check 'the refused instance never built a scene to show' `
        (($rival.Code -eq 0) -and (($rival.Out + $rival.Err) -notmatch '\[qa\] ')) `
        ('code=' + $rival.Code)
    Check 'the rival refused without touching the holder window' `
        ($holderWindow.Handle -ne [IntPtr]::Zero) ('source=' + $holderWindow.Source)
    $closed = Stop-Zuuned -Handle $holder
    if ($closed.Killed) { $script:killed++ }
    Check 'the instance that holds the lock still closes politely' `
        (-not $closed.Killed) ('code=' + $closed.Code)
    # The refusal comes after the session log opens, so the shared profile keeps a
    # second log file and the rival prints that it could not capture the first one.
    # Both are the app's existing order of operations: diagnostics before locks.
    $logs = @(Get-ChildItem -LiteralPath (Join-Path $sharedDir 'State\logs') -File -Force `
        -ErrorAction SilentlyContinue | Where-Object { $_.Name -like 'zuuned-*.log' })
    Write-Host ('  note the shared profile holds ' + $logs.Count + ' session log(s);' +
        ' the lock, not the exit code (' + $rival.Code + '), is what the check reads')
}

Write-Host ''
Write-Host '== profile isolation'
if (Select-Phase 'profile') {
    $fileCase = Invoke-Case -Name 'profile-files' -Arguments @('--page', 'music')
    Assert-Case -Case $fileCase -Label 'a fresh profile launch is healthy and shows onboarding' `
        -ExpectPage 'music' -ExpectOnboarding 'true'
    $logs = @(Get-ChildItem -LiteralPath (Join-Path $fileCase.Profile 'State\logs') -File -Force `
        -ErrorAction SilentlyContinue)
    $written = @($logs | Where-Object { $_.Name -like 'zuuned-*.log' -and $_.Length -gt 0 })
    Check 'the isolated profile owns a non-empty session log' `
        ($written.Count -gt 0) ('logs=' + $logs.Count)
    Check 'the isolated profile owns the settings ini' `
        (Test-Path -LiteralPath (Join-Path $fileCase.Profile 'Zuuned\Zuuned.ini')) $fileCase.Profile
    Check 'the isolated profile owns both databases' `
        ((Test-Path -LiteralPath (Join-Path $fileCase.Profile 'library.db')) -and
            (Test-Path -LiteralPath (Join-Path $fileCase.Profile 'spoken\spoken.db'))) $fileCase.Profile
    Check 'the isolated profile owns the single-instance lock' `
        (Test-Path -LiteralPath (Join-Path $fileCase.Profile 'State\logs\zuuned.lock')) $fileCase.Profile

    # The point of the whole run.  The user's own running Zuuned checkpoints its own
    # library.db-wal throughout, so a byte-for-byte compare would accuse this gate of
    # damage it did not do; instead nothing this run started may appear in the real
    # profile, under any of the process ids it launched.
    $byPid = @($script:pids | ForEach-Object { [regex]::Escape('-' + $_ + '-') })
    $mine = @()
    if ($byPid.Count) {
        $pattern = ($byPid -join '|')
        $mine = @(Get-RealProfileFiles | Where-Object { $_.Name -match $pattern })
    }
    Check 'no process this gate started wrote into the real profile' `
        ($mine.Count -eq 0) (($mine | ForEach-Object { $_.FullName }) -join ' ')
    $born = @(Get-RealProfileFiles -Since $script:runStart)
    Check 'the real profile gained no file while the gate ran' `
        ($born.Count -eq 0) (($born | ForEach-Object { $_.Name }) -join ' ')
    # Qt's automatic shader/pipeline cache is the one profile file this app does not
    # open itself - Qt6Quick asks QStandardPaths::CacheLocation directly - and a file
    # that already exists only changes mtime, so the creation-time check above cannot
    # see it.  Read the timestamp instead.  The app now names the cache path for Qt
    # inside the profile (src/main.cpp), so a gate launch must leave the real one
    # completely alone.
    $realCacheRoot = Join-Path $env:LOCALAPPDATA 'Zuuned\Zuuned\cache'
    $touchedRealCache = @(Get-ChildItem -LiteralPath $realCacheRoot -Recurse -File -Force `
        -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTimeUtc -ge $script:runStart })
    Check 'Qt pipeline cache stayed out of the real profile' `
        ($touchedRealCache.Count -eq 0) (($touchedRealCache | ForEach-Object { $_.Name }) -join ' ')
    $ownCache = @(Get-ChildItem -LiteralPath (Join-Path $fileCase.Profile 'cache') -Recurse -File -Force `
        -ErrorAction SilentlyContinue)
    Check 'the isolated profile owns the Qt pipeline cache' `
        ($ownCache.Count -gt 0) (($ownCache | ForEach-Object { $_.Name + '=' + $_.Length }) -join ' ')
    Write-Host ('  note real profile before the run: ' + $beforeFp)
    Write-Host ('  note real profile after the run:  ' + (Get-RealProfileFingerprint))
}

Write-Host ''
Write-Host '== summary'
if ($script:killed -eq 0) {
    Check 'every launch this gate made closed on its own' $true
}
else {
    Check 'every launch this gate made closed on its own' $false ('killed=' + $script:killed)
}
Write-Host ('launches=' + $script:caseSeq + ' checks=' + $script:checks +
    ' failures=' + $script:failures + ' skips=' + $script:skips +
    ' killed=' + $script:killed + ' pid-scan-fallbacks=' + $script:pidScans)
if ($script:noise.Count) {
    Write-Host ('recorded diagnostics (' + $script:noise.Count + '), not asserted:')
    foreach ($n in @($script:noise | Select-Object -First 25)) { Write-Host ('  note ' + $n) }
}
if ($script:failures) {
    foreach ($f in $script:failed) { Write-Host ('  FAILED ' + $f) }
    Write-Host 'OVERALL=fail'
    exit 1
}
Write-Host ('scope: ' + $script:caseSeq + ' instrumented launches, no device and no audio output')
Write-Host 'NOT YET HARDWARE-TESTED: no Zune has ever been connected to a Windows build'
Write-Host 'OVERALL=ok'
exit 0
