# ==============================================================================
# SuperSearch v2.6.0 - Multi-Tool Head-to-Head Performance Benchmark Suite
# ==============================================================================
# Compares SuperSearch (Standard & --cache) against:
#   - ripgrep (rg)
#   - git grep
#   - GNU grep
#   - fd (filename search comparison)
#   - Windows findstr
# across 10 realistic search scenarios on both a Many-File Source Tree Corpus
# and a Large-File High-Bandwidth MMap Corpus.
# ==============================================================================

param(
    [int]$Iterations = 5,
    [int]$SourceFileCount = 6000,
    [int]$LargeFileCount = 12,
    [int]$LargeFileSizeMB = 20
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SuperExe    = Join-Path $ProjectRoot "bin\supersearch.exe"

if (-not (Test-Path $SuperExe)) {
    throw "SuperSearch binary not found at $SuperExe. Run build.bat first."
}

# Resolve competitor binaries
function Find-Binary([string]$Name, [string[]]$FallbackPaths) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd -and (Test-Path $cmd.Source)) {
        return $cmd.Source
    }
    foreach ($p in $FallbackPaths) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

$RgExe      = Find-Binary "rg"      @("$env:USERPROFILE\.pi\agent\bin\rg.exe", "$env:LOCALAPPDATA\Microsoft\WinGet\Links\rg.exe")
$GrepExe    = Find-Binary "grep"    @("$env:ProgramFiles\Git\usr\bin\grep.exe", "$env:SystemDrive\msys64\usr\bin\grep.exe")
$GitExe     = Find-Binary "git"     @("$env:ProgramFiles\Git\cmd\git.exe")
$FdExe      = Find-Binary "fd"      @("$env:USERPROFILE\.pi\agent\bin\fd.exe", "$env:LOCALAPPDATA\Microsoft\WinGet\Links\fd.exe")
$FindstrExe = Find-Binary "findstr" @("$env:SystemRoot\System32\findstr.exe")

$CpuName = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Name)
$LogicalCores = [Environment]::ProcessorCount

Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  SUPERSEARCH v2.6.0 - HEAD-TO-HEAD COMPETITOR BENCHMARK SUITE" -ForegroundColor Cyan
Write-Host "==============================================================================" -ForegroundColor Cyan
Write-Host "  CPU           : $CpuName ($LogicalCores Logical Cores)" -ForegroundColor White
Write-Host "  Iterations    : 1 Warm-up + $Iterations Measured Runs per Tool/Scenario" -ForegroundColor White
Write-Host "  SuperSearch   : $SuperExe" -ForegroundColor Green
Write-Host "  ripgrep (rg)  : $(if ($RgExe) { $RgExe } else { 'NOT FOUND' })" -ForegroundColor Yellow
Write-Host "  git grep      : $(if ($GitExe) { $GitExe } else { 'NOT FOUND' })" -ForegroundColor Yellow
Write-Host "  GNU grep      : $(if ($GrepExe) { $GrepExe } else { 'NOT FOUND' })" -ForegroundColor Yellow
Write-Host "  fd-find (fd)  : $(if ($FdExe) { $FdExe } else { 'NOT FOUND' })" -ForegroundColor Yellow
Write-Host "  findstr       : $(if ($FindstrExe) { $FindstrExe } else { 'NOT FOUND' })" -ForegroundColor Yellow
Write-Host "==============================================================================`n" -ForegroundColor Cyan

# High-precision process runner redirecting stdout to a scratch file to avoid pipe buffer deadlocks
$ScratchOut = Join-Path ([System.IO.Path]::GetTempPath()) "ss_bench_stdout.tmp"
$ScratchErr = Join-Path ([System.IO.Path]::GetTempPath()) "ss_bench_stderr.tmp"

function Invoke-MeasuredProcess {
    param(
        [string]$FilePath,
        [string]$ArgumentString,
        [string]$WorkingDir
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = $ArgumentString
    $psi.WorkingDirectory = $WorkingDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$proc.Start()
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $proc.WaitForExit()
    $outText = $outTask.Result
    [void]$errTask.Result
    $sw.Stop()

    return [PSCustomObject]@{
        ElapsedMs = $sw.Elapsed.TotalMilliseconds
        OutText   = $outText
    }
}

function Measure-CommandProcess {
    param(
        [string]$FilePath,
        [string]$ArgumentString,
        [string]$WorkingDir = $ProjectRoot,
        [int]$Runs = $Iterations
    )

    # 1. Warm-up run
    $warmRes = Invoke-MeasuredProcess -FilePath $FilePath -ArgumentString $ArgumentString -WorkingDir $WorkingDir
    
    $times = New-Object System.Collections.Generic.List[double]
    $lastOut = $warmRes.OutText
    for ($r = 1; $r -le $Runs; $r++) {
        $runRes = Invoke-MeasuredProcess -FilePath $FilePath -ArgumentString $ArgumentString -WorkingDir $WorkingDir
        $times.Add($runRes.ElapsedMs)
        $lastOut = $runRes.OutText
    }

    $sorted = $times | Sort-Object
    $minMs  = [math]::Round($sorted[0], 2)
    $medMs  = [math]::Round($sorted[[int][math]::Floor($sorted.Count / 2)], 2)
    $avgMs  = [math]::Round(($times | Measure-Object -Average).Average, 2)

    $outLines = 0
    if (-not [string]::IsNullOrEmpty($lastOut)) {
        $outLines = ($lastOut -split "`n").Count - 1
        if ($outLines -lt 1) { $outLines = 1 }
    }

    return [PSCustomObject]@{
        MinMs     = $minMs
        MedianMs  = $medMs
        AvgMs     = $avgMs
        OutLines  = $outLines
    }
}

$BenchRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("supersearch_bench_" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
$CorpusTreeDir  = Join-Path $BenchRoot "corpus_tree"
$CorpusLargeDir = Join-Path $BenchRoot "corpus_large"

$AllResults = New-Object System.Collections.Generic.List[object]

try {
    # ==========================================================================
    # STEP 1: Generate Synthetic Corpora (Fast In-Memory Buffer Generation)
    # ==========================================================================
    Write-Host "[1/3] Generating Benchmark Corpora in $BenchRoot ..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $CorpusTreeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $CorpusLargeDir -Force | Out-Null

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)

    # Create sample source templates (~12 KB each)
    $exts = @(".c", ".h", ".py", ".ts", ".cs", ".json", ".md")
    $baseBlock = @"
#include <stdio.h>
#include <stdlib.h>
// Standard module header comment for performance benchmarking
int calculate_metrics_alpha(int input_a, int input_b) {
    int local_acc = input_a * 42 + input_b;
    const char* status_msg = "Operation completed normally in worker module";
    return local_acc;
}
"@
    # Repeat baseBlock to make each source file ~12 KB
    $standardFileBody = ($baseBlock * 45)

    $totalTreeBytes = 0L
    $numSubDirs = 60
    $subDirs = for ($d = 0; $d -lt $numSubDirs; $d++) {
        $sd = Join-Path $CorpusTreeDir ("module_" + $d.ToString("D3") + "\src")
        New-Item -ItemType Directory -Path $sd -Force | Out-Null
        $sd
    }

    for ($f = 1; $f -le $SourceFileCount; $f++) {
        $dirPath = $subDirs[$f % $numSubDirs]
        $ext = $exts[$f % $exts.Count]
        $fileName = if ($f -eq 1234) {
            "special_findme_controller_1234$ext"
        } elseif ($f -eq 4321) {
            "special_findme_service_4321$ext"
        } else {
            "component_" + $f.ToString("D5") + $ext
        }
        $fullPath = Join-Path $dirPath $fileName

        $extraLine = ""
        if ($f -eq 500 -or $f -eq 2500 -or $f -eq 5000) {
            $extraLine = "`n// BENCH_RARE_NEEDLE_2026 located in file $f`n"
        }
        if ($f % 200 -eq 0) {
            $extraLine += "`nconst char* token = `"CaseTest_Mixed_Token_Value`";`n"
        }
        if ($f % 300 -eq 0) {
            $extraLine += "`n// Contact: admin_2026@supersearch.org or user_99@example.com`n"
        }
        if ($f % 150 -eq 0) {
            $extraLine += "`n// Status codes: ERR_TIMEOUT_SOCKET or ERR_AUTH_INVALID`n"
        }

        $content = $standardFileBody + $extraLine
        [System.IO.File]::WriteAllText($fullPath, $content, $utf8NoBom)
        $totalTreeBytes += $content.Length
    }

    $treeMB = [math]::Round($totalTreeBytes / 1MB, 2)
    Write-Host "  -> Corpus 1 (Source Code Tree): $SourceFileCount files across $numSubDirs dirs ($treeMB MB)" -ForegroundColor Gray

    # Initialize git repo inside CorpusTreeDir so `git grep` can be tested fairly
    if ($GitExe) {
        & $GitExe -C $CorpusTreeDir init -q 2>$null
    }

    # Generate Corpus 2: Large Data/Log Files (12 x 20 MB = 240 MB)
    $lineTemplate = "2026-10-15T12:00:00.000Z [INFO] [WorkerThread-04] Processing transaction batch id=9876543210 status=OK latency_us=142 payload_bytes=4096`r`n"
    $linesPerMB = [int][math]::Ceiling((1024 * 1024) / $lineTemplate.Length)
    $mbChunkBuilder = New-Object System.Text.StringBuilder(1100000)
    for ($i = 0; $i -lt $linesPerMB; $i++) {
        [void]$mbChunkBuilder.Append($lineTemplate)
    }
    $oneMBChunk = $mbChunkBuilder.ToString()

    $totalLargeBytes = 0L
    for ($lf = 1; $lf -le $LargeFileCount; $lf++) {
        $lfPath = Join-Path $CorpusLargeDir ("server_stream_" + $lf.ToString("D2") + ".log")
        $fs = New-Object System.IO.StreamWriter($lfPath, $false, $utf8NoBom, 65536)
        for ($m = 1; $m -le $LargeFileSizeMB; $m++) {
            $fs.Write($oneMBChunk)
            if ($lf -eq 6 -and $m -eq [int]($LargeFileSizeMB / 2)) {
                $fs.Write("2026-10-15T12:30:00.000Z [CRITICAL] LARGE_CORPUS_ULTRA_NEEDLE_2026 detected in stream $lf`r`n")
            }
            if ($m -eq $LargeFileSizeMB) {
                $fs.Write("2026-10-15T12:59:59.000Z [WARN] Contact support: devops_42@enterprise.com for ERR_TIMEOUT_SOCKET`r`n")
            }
        }
        $fs.Close()
        $totalLargeBytes += (Get-Item $lfPath).Length
    }
    $largeMB = [math]::Round($totalLargeBytes / 1MB, 2)
    Write-Host "  -> Corpus 2 (Large MMap Files): $LargeFileCount files x ${LargeFileSizeMB}MB ($largeMB MB)`n" -ForegroundColor Gray

    # Pre-build SuperSearch binary cache for Corpus 1
    & $SuperExe --cache-build -d "$CorpusTreeDir" | Out-Null

    # Helper to record a benchmark scenario
    function Run-Scenario {
        param(
            [string]$ScenarioId,
            [string]$ScenarioName,
            [string]$CorpusName,
            [double]$CorpusSizeMB,
            [hashtable[]]$Competitors
        )
        Write-Host "------------------------------------------------------------------------------" -ForegroundColor DarkCyan
        Write-Host "  [$ScenarioId] $ScenarioName ($CorpusName - $CorpusSizeMB MB)" -ForegroundColor Cyan
        Write-Host "------------------------------------------------------------------------------" -ForegroundColor DarkCyan

        $scenarioRows = New-Object System.Collections.Generic.List[object]
        foreach ($comp in $Competitors) {
            if (-not $comp.Enabled) { continue }
            $res = Measure-CommandProcess -FilePath $comp.Exe -ArgumentString $comp.Args -WorkingDir $comp.WorkDir
            $throughputMBs = if ($res.MinMs -gt 0) { [math]::Round(($CorpusSizeMB / ($res.MinMs / 1000.0)), 1) } else { 0 }
            $row = [PSCustomObject]@{
                ScenarioId   = $ScenarioId
                Scenario     = $ScenarioName
                Tool         = $comp.Name
                MinMs        = $res.MinMs
                MedianMs     = $res.MedianMs
                AvgMs        = $res.AvgMs
                ThroughputMB = $throughputMBs
                OutputLines  = $res.OutLines
            }
            $scenarioRows.Add($row)
            $AllResults.Add($row)
            Write-Host ("    {0,-28} | Min: {1,7} ms | Med: {2,7} ms | Avg: {3,7} ms | {4,8} MB/s" -f $comp.Name, $res.MinMs, $res.MedianMs, $res.AvgMs, $throughputMBs) -ForegroundColor $(if ($comp.Name -like "SuperSearch*") { "Green" } else { "Gray" })
        }
    }

    Write-Host "[2/3] Executing 8 Head-to-Head Benchmark Scenarios..." -ForegroundColor Cyan

    # --------------------------------------------------------------------------
    # S1: Rare Literal Search on Corpus 1 (6,000 files)
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S1" -ScenarioName "Rare Literal Search (Many Files)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (Standard)"; Exe = $SuperExe; Args = "`"$CorpusTreeDir`" `"BENCH_RARE_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (--cache)";  Exe = $SuperExe; Args = "--cache `"$CorpusTreeDir`" `"BENCH_RARE_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg)";           Exe = $RgExe;    Args = "-n --column --no-heading --color=never `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "git grep (untracked)";   Exe = $GitExe;   Args = "-C `"$CorpusTreeDir`" grep --untracked -n --column `"BENCH_RARE_NEEDLE_2026`""; WorkDir = $CorpusTreeDir; Enabled = ($null -ne $GitExe) },
        @{ Name = "GNU grep";               Exe = $GrepExe;  Args = "-rn `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) },
        @{ Name = "Windows findstr";        Exe = $FindstrExe; Args = "/S /N /C:`"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir\*`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $FindstrExe) }
    )

    # --------------------------------------------------------------------------
    # S2: Rare Literal Search on Corpus 2 (Large MMap Files - Pure SIMD Bandwidth)
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S2" -ScenarioName "Large File MMap + SIMD Bandwidth" -CorpusName "Corpus 2 ($LargeFileCount x ${LargeFileSizeMB}MB)" -CorpusSizeMB $largeMB -Competitors @(
        @{ Name = "SuperSearch (MMap+AVX2)"; Exe = $SuperExe; Args = "`"$CorpusLargeDir`" `"LARGE_CORPUS_ULTRA_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg)";            Exe = $RgExe;    Args = "-n --column --no-heading --color=never `"LARGE_CORPUS_ULTRA_NEEDLE_2026`" `"$CorpusLargeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "GNU grep";                Exe = $GrepExe;  Args = "-rn `"LARGE_CORPUS_ULTRA_NEEDLE_2026`" `"$CorpusLargeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) },
        @{ Name = "Windows findstr";         Exe = $FindstrExe; Args = "/S /N /C:`"LARGE_CORPUS_ULTRA_NEEDLE_2026`" `"$CorpusLargeDir\*`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $FindstrExe) }
    )

    # --------------------------------------------------------------------------
    # S3: Case-Insensitive Search (-i) on Corpus 1
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S3" -ScenarioName "Case-Insensitive Search (-i)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (-i)";        Exe = $SuperExe; Args = "-i `"$CorpusTreeDir`" `"casetest_mixed_token_value`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (-i --cache)"; Exe = $SuperExe; Args = "-i --cache `"$CorpusTreeDir`" `"casetest_mixed_token_value`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg -i)";         Exe = $RgExe;    Args = "-i -n --column --no-heading --color=never `"casetest_mixed_token_value`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "git grep (-i)";           Exe = $GitExe;   Args = "-C `"$CorpusTreeDir`" grep --untracked -i -n `"casetest_mixed_token_value`""; WorkDir = $CorpusTreeDir; Enabled = ($null -ne $GitExe) },
        @{ Name = "GNU grep (-i)";           Exe = $GrepExe;  Args = "-rni `"casetest_mixed_token_value`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) }
    )

    # --------------------------------------------------------------------------
    # S4: Regular Expression Search (-R) on Corpus 1
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S4" -ScenarioName "Regular Expression Search (-R)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (-R)";        Exe = $SuperExe; Args = "-R `"$CorpusTreeDir`" `"(admin|user)_[0-9]{2,4}@[a-z]+\.(org|com)`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (-R --cache)"; Exe = $SuperExe; Args = "-R --cache `"$CorpusTreeDir`" `"(admin|user)_[0-9]{2,4}@[a-z]+\.(org|com)`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg regex)";      Exe = $RgExe;    Args = "-n --column --no-heading --color=never `"(admin|user)_[0-9]{2,4}@[a-z]+\.(org|com)`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "GNU grep (-E)";           Exe = $GrepExe;  Args = "-rnE `"(admin|user)_[0-9]{2,4}@[a-z]+\.(org|com)`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) }
    )

    # --------------------------------------------------------------------------
    # S5: Multi-Pattern OR Search (-e p1 -e p2) on Corpus 1
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S5" -ScenarioName "Multi-Pattern Search (-e OR)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (-e x2)";        Exe = $SuperExe; Args = "-d `"$CorpusTreeDir`" -e `"ERR_TIMEOUT_SOCKET`" -e `"BENCH_RARE_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (-e x2 --cache)"; Exe = $SuperExe; Args = "--cache -d `"$CorpusTreeDir`" -e `"ERR_TIMEOUT_SOCKET`" -e `"BENCH_RARE_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg -e x2)";         Exe = $RgExe;    Args = "-n --column --no-heading --color=never -e `"ERR_TIMEOUT_SOCKET`" -e `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "GNU grep (-e x2)";           Exe = $GrepExe;  Args = "-rn -e `"ERR_TIMEOUT_SOCKET`" -e `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) }
    )

    # --------------------------------------------------------------------------
    # S6: Language Type Filtered Search (-t py) on Corpus 1
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S6" -ScenarioName "Language Type Filter (-t py)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (-t py)";    Exe = $SuperExe; Args = "-t py `"$CorpusTreeDir`" `"BENCH_RARE_NEEDLE_2026`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg -t py)";     Exe = $RgExe;    Args = "-t py -n --column --no-heading --color=never `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "GNU grep (--include)";   Exe = $GrepExe;  Args = "-rn --include=`"*.py`" `"BENCH_RARE_NEEDLE_2026`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) }
    )

    # --------------------------------------------------------------------------
    # S7: Dual-Target Discovery (Filename + Content) vs fd + rg
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S7" -ScenarioName "Filename + Content Dual Discovery" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (Dual-Target)"; Exe = $SuperExe; Args = "`"$CorpusTreeDir`" `"special_findme_`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (Dual --cache)"; Exe = $SuperExe; Args = "--cache `"$CorpusTreeDir`" `"special_findme_`" --porcelain"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "fd (Filename Only)";        Exe = $FdExe;    Args = "--color=never `"special_findme_`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $FdExe) },
        @{ Name = "ripgrep (Content Only)";    Exe = $RgExe;    Args = "-n --column --no-heading --color=never `"special_findme_`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) }
    )

    # --------------------------------------------------------------------------
    # S8: High-Frequency Match with Early Cap (--max-results 50)
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S8" -ScenarioName "High-Frequency + Early Halt (50 cap)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (--max-results 50)"; Exe = $SuperExe; Args = "--max-results 50 `"$CorpusTreeDir`" `"calculate_metrics_alpha`" --json-stdout"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (--cache --max 50)"; Exe = $SuperExe; Args = "--cache --max-results 50 `"$CorpusTreeDir`" `"calculate_metrics_alpha`" --json-stdout"; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg --json -m 50)";      Exe = $RgExe;    Args = "--json -m 50 `"calculate_metrics_alpha`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) }
    )

    # --------------------------------------------------------------------------
    # S9: AI Agent File Tree Enumeration (--files vs rg --files vs fd)
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S9" -ScenarioName "AI Agent File List (--files)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (--files)";          Exe = $SuperExe; Args = "--files `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (--files --cache)";  Exe = $SuperExe; Args = "--cache --files `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg --files)";           Exe = $RgExe;    Args = "--files `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "fd (All Files)";                 Exe = $FdExe;    Args = "--color=never --type f . `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $FdExe) }
    )

    # --------------------------------------------------------------------------
    # S10: AI Agent Files-With-Matches (-l / --files-with-matches)
    # --------------------------------------------------------------------------
    Run-Scenario -ScenarioId "S10" -ScenarioName "Files With Matches (-l Early Stop)" -CorpusName "Corpus 1 ($SourceFileCount files)" -CorpusSizeMB $treeMB -Competitors @(
        @{ Name = "SuperSearch (-l)";               Exe = $SuperExe; Args = "-l `"calculate_metrics_alpha`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "SuperSearch (-l --cache)";       Exe = $SuperExe; Args = "--cache -l `"calculate_metrics_alpha`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = $true },
        @{ Name = "ripgrep (rg -l)";                Exe = $RgExe;    Args = "-l `"calculate_metrics_alpha`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $RgExe) },
        @{ Name = "GNU grep (-rl)";                 Exe = $GrepExe;  Args = "-rl `"calculate_metrics_alpha`" `"$CorpusTreeDir`""; WorkDir = $ProjectRoot; Enabled = ($null -ne $GrepExe) }
    )

    # Export JSON results to tests/benchmark_results.json
    $OutJsonFile = Join-Path $PSScriptRoot "benchmark_results.json"
    $AllResults | ConvertTo-Json -Depth 5 | Set-Content -Path $OutJsonFile -Encoding UTF8
    Write-Host "`n[3/3] Benchmark results exported to $OutJsonFile" -ForegroundColor Green

} finally {
    # Clean up cache and temporary benchmark files
    if (Test-Path $CorpusTreeDir) {
        & $SuperExe --cache-clear -d "$CorpusTreeDir" 2>&1 | Out-Null
    }
    if (Test-Path $BenchRoot) {
        Remove-Item -Path $BenchRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -Path $ScratchOut, $ScratchErr -Force -ErrorAction SilentlyContinue
}
