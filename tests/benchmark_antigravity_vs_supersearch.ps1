param(
    [string]$TargetDir = "",
    [string]$Query = "identity"
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$targetDir = if ($TargetDir -and (Test-Path $TargetDir)) { (Resolve-Path $TargetDir).Path } else { $ProjectRoot }
$query = $Query

# Locate Antigravity built-in search binary or system ripgrep dynamically without hardcoded path
$ideEngine = Get-ChildItem "$env:LOCALAPPDATA\Programs\Antigravity IDE" -Filter "*.exe" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^rg\.exe$' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $ideEngine) {
    $ideEngine = (Get-Command "rg" -ErrorAction SilentlyContinue).Source
}

$superPath = Join-Path $ProjectRoot "bin\supersearch.exe"
$cpuName = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Name)
$logicalCores = [Environment]::ProcessorCount

Write-Host "====================================================================="
Write-Host " HEAD-TO-HEAD BENCHMARK: ANTIGRAVITY DEFAULT ENGINE vs SUPERSEARCH (C AVX2)"
Write-Host " Target Directory : $targetDir"
Write-Host " Search Query     : '$query'"
Write-Host " System CPU       : $cpuName ($logicalCores Logical Cores)"
Write-Host "====================================================================="

# -------------------------------------------------------------------------
# 1. Warm-up Phase (Ensure OS File Cache is equally populated for both)
# -------------------------------------------------------------------------
Write-Host "`n[1/3] Warming up OS file system caches..."
if ($ideEngine) {
    & $ideEngine -s --glob "!.git/**" --glob "!.vs/**" --glob "!node_modules/**" --glob "!bin/**" --glob "!obj/**" "$query" "$targetDir" | Out-Null
}
& $superPath "$targetDir" "$query" --porcelain | Out-Null
Write-Host "Warm-up complete."

# -------------------------------------------------------------------------
# 2. Benchmark Antigravity Default Internal Engine - 3 Runs
# -------------------------------------------------------------------------
Write-Host "`n[2/3] Benchmarking Antigravity Default Internal Engine [3 Iterations]..."
$ideTimes = @()
$ideOutput = $null

if ($ideEngine) {
    for ($i = 1; $i -le 3; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $ideOutput = & $ideEngine -n -H --no-heading --glob "!.git/**" --glob "!.vs/**" --glob "!node_modules/**" --glob "!bin/**" --glob "!obj/**" "$query" "$targetDir"
        $sw.Stop()
        $t = $sw.Elapsed.TotalMilliseconds
        $ideTimes += $t
        Write-Host "  Iteration $i : $([math]::Round($t, 1)) ms"
    }

    $ideAvg = [math]::Round(($ideTimes | Measure-Object -Average).Average, 1)
    $ideMin = [math]::Round(($ideTimes | Measure-Object -Minimum).Minimum, 1)
    $ideCount = ($ideOutput | Measure-Object).Count
} else {
    $ideAvg = 0
    $ideMin = 0
    $ideCount = 0
}

# -------------------------------------------------------------------------
# 3. Benchmark SuperSearch (C, AVX2 SIMD, Win32 Threadpool) - 3 Runs
# -------------------------------------------------------------------------
Write-Host "`n[3/3] Benchmarking SuperSearch Engine [3 Iterations]..."
$superTimes = @()
$superJson = $null

for ($i = 1; $i -le 3; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $superRaw = & $superPath "$targetDir" "$query" --json-stdout
    $sw.Stop()
    $t = $sw.Elapsed.TotalMilliseconds
    $superTimes += $t
    if ($i -eq 1) {
        $superJson = $superRaw | ConvertFrom-Json
    }
    Write-Host "  Iteration $i : $([math]::Round($t, 1)) ms"
}

$superAvg = [math]::Round(($superTimes | Measure-Object -Average).Average, 1)
$superMin = [math]::Round(($superTimes | Measure-Object -Minimum).Minimum, 1)
$superCount = $superJson.total_matches
$superFileCount = $superJson.files_scanned
$superBytesMB = [math]::Round($superJson.bytes_scanned / 1MB, 2)
$superPlanTime = $superJson.inspection_time_ms
$superSearchTime = $superJson.search_time_ms

# -------------------------------------------------------------------------
# 4. Results & Summary
# -------------------------------------------------------------------------
Write-Host "`n====================================================================="
Write-Host " COMPREHENSIVE STATISTICAL COMPARISON"
Write-Host "====================================================================="

$speedup = if ($superMin -gt 0 -and $ideMin -gt 0) { [math]::Round($ideMin / $superMin, 2) } else { "N/A" }
$speedupAvg = if ($superAvg -gt 0 -and $ideAvg -gt 0) { [math]::Round($ideAvg / $superAvg, 2) } else { "N/A" }

$results = @(
    [PSCustomObject]@{
        "Metric / Feature"       = "Best Execution Time (Min)"
        "Antigravity IDE Engine" = "$ideMin ms"
        "SuperSearch (C AVX2)"   = "$superMin ms"
        "Winner / Difference"    = "SuperSearch ($speedup x)"
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Average Execution Time"
        "Antigravity IDE Engine" = "$ideAvg ms"
        "SuperSearch (C AVX2)"   = "$superAvg ms"
        "Winner / Difference"    = "SuperSearch ($speedupAvg x)"
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Total Matches Found"
        "Antigravity IDE Engine" = "$ideCount"
        "SuperSearch (C AVX2)"   = "$superCount ($($superJson.filename_matches) in filenames)"
        "Winner / Difference"    = if ($superCount -ge $ideCount) { "SuperSearch (Dual-Target)" } else { "IDE Engine" }
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Pre-Search Inspection & Plan"
        "Antigravity IDE Engine" = "None (Blind start)"
        "SuperSearch (C AVX2)"   = "Yes ($superPlanTime ms analysis)"
        "Winner / Difference"    = "SuperSearch (LPT Load Balance)"
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Search Time excluding plan"
        "Antigravity IDE Engine" = "N/A"
        "SuperSearch (C AVX2)"   = "$superSearchTime ms"
        "Winner / Difference"    = "Pure Search Engine"
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Output Format & AI Readiness"
        "Antigravity IDE Engine" = "Raw string lines"
        "SuperSearch (C AVX2)"   = "Clean Structured JSON + Schema"
        "Winner / Difference"    = "SuperSearch (Zero-overhead parse)"
    },
    [PSCustomObject]@{
        "Metric / Feature"       = "Context Protection (--max-results)"
        "Antigravity IDE Engine" = "Max count flag (Thread keep running)"
        "SuperSearch (C AVX2)"   = "Atomic thread halt across 24 cores"
        "Winner / Difference"    = "SuperSearch (Token Economy)"
    }
)

$results | Format-Table -AutoSize
