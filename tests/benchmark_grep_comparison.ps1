param(
    [string]$TargetDir = "",
    [string]$Query = "identity"
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$targetDir = if ($TargetDir -and (Test-Path $TargetDir)) { (Resolve-Path $TargetDir).Path } else { $ProjectRoot }
$superExe = Join-Path $ProjectRoot "bin\supersearch.exe"
$query = $Query

$scratchSuper = Join-Path ([System.IO.Path]::GetTempPath()) "scratch_supersearch.json"
$scratchGrep  = Join-Path ([System.IO.Path]::GetTempPath()) "scratch_grep.txt"

Write-Host "=========================================================="
Write-Host " BENCHMARK: $targetDir - Query: '$query'"
Write-Host "=========================================================="

# 1. SuperSearch
Write-Host "`n[1/3] Running SuperSearch..."
$sw1 = [System.Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $superExe `
    -ArgumentList "`"$targetDir`" `"$query`" --json-stdout" `
    -NoNewWindow -Wait -PassThru -RedirectStandardOutput $scratchSuper
$sw1.Stop()

$supersearchTime = $sw1.Elapsed.TotalSeconds
$superJson = Get-Content $scratchSuper -Raw | ConvertFrom-Json
$supersearchMatches = $superJson.total_matches
$supersearchFiles = $superJson.files_scanned
$supersearchVolumeMB = [math]::Round($superJson.bytes_scanned / 1MB, 2)

Write-Host "SuperSearch Completed:"
Write-Host "  Elapsed Time    : $([math]::Round($supersearchTime, 3)) s"
Write-Host "  Matches Found   : $supersearchMatches"
Write-Host "  Files Scanned   : $supersearchFiles"
Write-Host "  Volume Scanned  : $supersearchVolumeMB MB"

# 2. GNU grep (Dynamic resolution)
$grepExe = (Get-Command "grep" -ErrorAction SilentlyContinue).Source
if (-not $grepExe) {
    foreach ($candidate in @("$env:ProgramFiles\Git\usr\bin\grep.exe", "$env:SystemDrive\msys64\usr\bin\grep.exe")) {
        if (Test-Path $candidate) { $grepExe = $candidate; break }
    }
}

$grepTime = 0
$grepMatches = 0
if ($grepExe) {
    Write-Host "`n[2/3] Running GNU grep ($grepExe)..."
    $sw2 = [System.Diagnostics.Stopwatch]::StartNew()
    $grepProc = Start-Process -FilePath $grepExe `
        -ArgumentList "-rn --exclude-dir=.git --exclude-dir=.vs --exclude-dir=node_modules --exclude-dir=bin --exclude-dir=obj `"$query`" `"$targetDir`"" `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $scratchGrep
    $sw2.Stop()
    $grepTime = $sw2.Elapsed.TotalSeconds
    $grepMatches = (Get-Content $scratchGrep | Measure-Object).Count

    Write-Host "GNU grep Completed:"
    Write-Host "  Elapsed Time    : $([math]::Round($grepTime, 3)) s"
    Write-Host "  Matches Found   : $grepMatches"
} else {
    Write-Host "`n[2/3] Skipping GNU grep (not found on system)." -ForegroundColor Yellow
}

# 3. Windows PowerShell Select-String
Write-Host "`n[3/3] Running PowerShell Select-String (Sample 1000 files)..."
$sw3 = [System.Diagnostics.Stopwatch]::StartNew()
$psMatches = (Get-ChildItem -Path $targetDir -Recurse -File -Exclude *.git* | Select-Object -First 1000 | Select-String -Pattern $query -SimpleMatch).Count
$sw3.Stop()
$psTimeSample = $sw3.Elapsed.TotalSeconds
$psEstimatedFull = if ($supersearchFiles -gt 0) { [math]::Round(($psTimeSample / [math]::Min(1000, $supersearchFiles)) * $supersearchFiles, 1) } else { 0 }

Write-Host "PowerShell Select-String:"
Write-Host "  Sample Time           : $([math]::Round($psTimeSample, 3)) s"
Write-Host "  Estimated Full Scan   : $psEstimatedFull s"

Remove-Item $scratchSuper, $scratchGrep -Force -ErrorAction SilentlyContinue

Write-Host "`n=========================================================="
Write-Host " BENCHMARK SUMMARY TABLE"
Write-Host "=========================================================="
$speedVsGrep = if ($grepTime -gt 0 -and $supersearchTime -gt 0) { "$([math]::Round($grepTime / $supersearchTime, 1))x FASTER" } else { "N/A" }

[PSCustomObject]@{
    Tool = "SuperSearch (SIMD AVX2 + Lock-Free)"
    "Total Time (s)" = [math]::Round($supersearchTime, 3)
    "Matches" = $supersearchMatches
    "Speed vs grep" = $speedVsGrep
    "Quality / Context" = "JSON + Plan + Col/Line + Context (-C)"
} | Format-Table

if ($grepExe) {
    [PSCustomObject]@{
        Tool = "GNU grep (Single Threaded)"
        "Total Time (s)" = [math]::Round($grepTime, 3)
        "Matches" = $grepMatches
        "Speed vs grep" = "1.0x (Baseline)"
        "Quality / Context" = "Raw text only, no plan, no JSON"
    } | Format-Table
}
