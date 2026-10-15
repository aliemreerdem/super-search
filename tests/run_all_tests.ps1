# ==============================================================================
# SuperSearch v2.5.0 - Comprehensive End-to-End Automated Test Suite
# ==============================================================================
# Tests all CLI options, engine pathways, SIMD/scalar search, regex VM,
# semantic filtering, gitignore/superignore hierarchy, zip/gz archives,
# search & replace (dry-run, diff, in-place, backup), cache, watcher,
# AI agent token economy flags, MCP JSON-RPC 2.0 server, and exit codes.
# ==============================================================================

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ExePath     = Join-Path $ProjectRoot "bin\supersearch.exe"
$RegexTestC  = Join-Path $ProjectRoot "tests\test_regex.c"
$RegexSrcC   = Join-Path $ProjectRoot "src\regex_engine.c"
$UtilsSrcC   = Join-Path $ProjectRoot "src\utils.c"
$IncludeDir  = Join-Path $ProjectRoot "include"
$TempRoot    = Join-Path ([System.IO.Path]::GetTempPath()) ("supersearch_e2e_" + [Guid]::NewGuid().ToString("N").Substring(0, 8))

$Script:TotalTests  = 0
$Script:PassedTests = 0
$Script:FailedTests = 0
$Script:FailureList = @()

function Write-Section($Title) {
    Write-Host "`n==================================================================" -ForegroundColor Cyan
    Write-Host "  $Title" -ForegroundColor Cyan
    Write-Host "==================================================================" -ForegroundColor Cyan
}

function Assert-Condition {
    param(
        [string]$Name,
        [bool]$Condition,
        [string]$Details = ""
    )
    $Script:TotalTests++
    if ($Condition) {
        $Script:PassedTests++
        Write-Host "  [PASS] $Name" -ForegroundColor Green
    } else {
        $Script:FailedTests++
        $Script:FailureList += "$Name $(if ($Details) { "-> $Details" })"
        Write-Host "  [FAIL] $Name" -ForegroundColor Red
        if ($Details) {
            Write-Host "         Details: $Details" -ForegroundColor Yellow
        }
    }
}

function Invoke-SuperSearch {
    param(
        [string[]]$CliArgs,
        [string]$StdinText = ""
    )
    $hasStdin = -not [string]::IsNullOrEmpty($StdinText)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.WorkingDirectory = $ProjectRoot
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardInput = $hasStdin
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8

    $escapedArgs = foreach ($arg in $CliArgs) {
        if ($arg -match '[\s"]') {
            '"' + ($arg -replace '"', '\"') + '"'
        } else {
            $arg
        }
    }
    $psi.Arguments = ($escapedArgs -join " ")

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()

    if ($hasStdin) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        $sw = New-Object System.IO.StreamWriter($proc.StandardInput.BaseStream, $utf8NoBom)
        $sw.Write($StdinText)
        $sw.Flush()
        $sw.Close()
    }

    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    [void]$proc.WaitForExit(15000)
    if (-not $proc.HasExited) {
        $proc.Kill()
    }

    return [PSCustomObject]@{
        ExitCode = $proc.ExitCode
        Stdout   = $stdoutTask.Result
        Stderr   = $stderrTask.Result
    }
}

try {
    # --------------------------------------------------------------------------
    # 0. Fixture Setup
    # --------------------------------------------------------------------------
    Write-Section "0. Setting Up Isolated Test Fixtures ($TempRoot)"
    New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    function Write-FixtureFile([string]$RelPath, [string]$Content) {
        $fullPath = Join-Path $TempRoot $RelPath
        $dir = Split-Path -Parent $fullPath
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        [System.IO.File]::WriteAllText($fullPath, $Content, $utf8NoBom)
        return $fullPath
    }

    # C source file with comments, strings, functions, and code
    Write-FixtureFile "src\engine_sample.c" @"
#include <stdio.h>
// MAGIC_TOKEN inside a single-line comment
/*
 * MAGIC_TOKEN inside a block comment
 */
int calculate_magic_score(int input_val) {
    const char *msg = "MAGIC_TOKEN inside a string literal";
    int MAGIC_TOKEN = input_val + 42;
    return MAGIC_TOKEN;
}

void helper_routine(void) {
    printf("Hello World 2026-10-15\n");
}
"@ | Out-Null

    # Header file
    Write-FixtureFile "include\engine_sample.h" @"
#ifndef ENGINE_SAMPLE_H
#define ENGINE_SAMPLE_H
int calculate_magic_score(int input_val);
#endif
"@ | Out-Null

    # Python file
    Write-FixtureFile "scripts\worker_alpha.py" @"
# Python comment with PY_MARKER
def compute_alpha():
    text = "CaseTest_MixedCase_Token"
    return "ALPHA_UNIQUE_STRING"
"@ | Out-Null

    # Markdown file with multiline patterns
    Write-FixtureFile "docs\guide.md" @"
# SuperSearch Documentation
START_BLOCK
This spans across
multiple lines
END_BLOCK
Contact: user123@example.com
Phone: 555-1234
"@ | Out-Null

    # Special named file & directory for dual-target search
    Write-FixtureFile "special_module\findme_target_file.txt" @"
Plain content inside findme_target_file.
Line two has needle_alpha.
Line three has needle_beta.
Line four is clean.
"@ | Out-Null

    # Hidden file and directory
    Write-FixtureFile ".hidden_config" "SECRET_HIDDEN_KEY=99999`n" | Out-Null
    Write-FixtureFile ".hidden_dir\inside_hidden.txt" "SECRET_HIDDEN_KEY=88888`n" | Out-Null

    # Ignored files via .gitignore and .superignore
    Write-FixtureFile ".gitignore" @"
ignored_dir/
*.log
!important.log
"@ | Out-Null

    Write-FixtureFile ".superignore" @"
super_ignored.txt
"@ | Out-Null

    Write-FixtureFile "ignored_dir\skip_me.c" "IGNORED_CONTENT_TOKEN`n" | Out-Null
    Write-FixtureFile "logs\debug.log" "IGNORED_CONTENT_TOKEN_LOG`n" | Out-Null
    Write-FixtureFile "logs\important.log" "IMPORTANT_LOG_KEPT_BY_NEGATION`n" | Out-Null
    Write-FixtureFile "super_ignored.txt" "SUPER_IGNORED_TOKEN`n" | Out-Null

    # Excluded directory test
    Write-FixtureFile "vendor_custom\thirdparty.js" "VENDOR_JS_TOKEN`n" | Out-Null

    # Binary file (contains null bytes)
    $binPath = Join-Path $TempRoot "assets\sample.bin"
    New-Item -ItemType Directory -Path (Split-Path -Parent $binPath) -Force | Out-Null
    $binBytes = [byte[]](0x00, 0x01, 0x02, 0x00) + [System.Text.Encoding]::ASCII.GetBytes("BINARY_SECRET_MARKER") + [byte[]](0x00, 0xFF)
    [System.IO.File]::WriteAllBytes($binPath, $binBytes)

    # Large file (> 128 KB) to trigger mmap I/O path and SIMD 32-byte blocks
    $sb = New-Object System.Text.StringBuilder
    for ($i = 1; $i -le 3000; $i++) {
        if ($i -eq 1500) {
            [void]$sb.Append("Line $i : MMAP_LARGE_FILE_NEEDLE_1500 padding text for SIMD acceleration test`r`n")
        } elseif ($i -eq 2950) {
            [void]$sb.Append("Line $i : MMAP_LARGE_FILE_NEEDLE_2950 padding text for SIMD acceleration test`r`n")
        } else {
            [void]$sb.Append("Line $i : Standard filler text to make the file larger than 64KB threshold for mmap.`r`n")
        }
    }
    Write-FixtureFile "large\big_data.txt" $sb.ToString() | Out-Null

    # Long line file for --max-line-len truncation test
    $longLine = ("A" * 150) + "LONG_LINE_NEEDLE" + ("B" * 150) + "`n"
    Write-FixtureFile "docs\minified.js" $longLine | Out-Null

    # Archive fixtures (.zip and .gz)
    $zipSourceDir = Join-Path $TempRoot "zip_staging"
    New-Item -ItemType Directory -Path $zipSourceDir -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $zipSourceDir "archived_note.txt"), "Line 1`nZIP_ARCHIVE_SECRET_NEEDLE on line 2`nLine 3`n", $utf8NoBom)
    $zipPath = Join-Path $TempRoot "archives\bundle.zip"
    New-Item -ItemType Directory -Path (Split-Path -Parent $zipPath) -Force | Out-Null
    Compress-Archive -Path "$zipSourceDir\*" -DestinationPath $zipPath -Force
    Remove-Item -Path $zipSourceDir -Recurse -Force

    $gzPath = Join-Path $TempRoot "archives\compressed.txt.gz"
    $gzBytes = [System.Text.Encoding]::UTF8.GetBytes("Header line`nGZIP_STREAM_SECRET_NEEDLE on line 2`nFooter line`n")
    $fs = [System.IO.File]::Create($gzPath)
    $gzStream = New-Object System.IO.Compression.GZipStream($fs, [System.IO.Compression.CompressionMode]::Compress)
    $gzStream.Write($gzBytes, 0, $gzBytes.Length)
    $gzStream.Close()
    $fs.Close()

    Write-Host "  Fixtures created successfully." -ForegroundColor Gray

    # --------------------------------------------------------------------------
    # 1. Build Verification & C Unit Tests
    # --------------------------------------------------------------------------
    Write-Section "1. Build Verification & C Unit Tests"
    Assert-Condition "supersearch.exe binary exists" (Test-Path $ExePath) "Missing $ExePath"

    $testRegexExe = Join-Path $TempRoot "test_regex_runner.exe"
    $ErrorActionPreference = "Continue"
    $compileOut = & gcc -O2 -I"$IncludeDir" "$RegexTestC" "$RegexSrcC" "$UtilsSrcC" -o "$testRegexExe" 2>&1
    $compileExit = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    Assert-Condition "Compile C unit test (tests/test_regex.c)" ($compileExit -eq 0) ($compileOut -join "`n")
    if (Test-Path $testRegexExe) {
        $unitOut = & $testRegexExe 2>&1
        Assert-Condition "Run C regex unit test suite (19 assertions)" ($LASTEXITCODE -eq 0 -and ($unitOut -join "`n") -match "ALL REGEX ENGINE TESTS PASSED") ($unitOut -join "`n")
    }

    # --------------------------------------------------------------------------
    # 2. Help, Version, Type List, and Exit Codes
    # --------------------------------------------------------------------------
    Write-Section "2. CLI Metadata Flags & Standard Exit Codes"
    $resHelp = Invoke-SuperSearch @("--help")
    Assert-Condition "--help returns exit code 0 and usage banner" ($resHelp.ExitCode -eq 0 -and $resHelp.Stdout -match "USAGE:")

    $resVer = Invoke-SuperSearch @("--version")
    Assert-Condition "--version returns exit code 0 and version string" ($resVer.ExitCode -eq 0 -and $resVer.Stdout -match "SuperSearch v")

    $resTypes = Invoke-SuperSearch @("--type-list")
    Assert-Condition "--type-list lists supported language presets" ($resTypes.ExitCode -eq 0 -and $resTypes.Stdout -match "rust" -and $resTypes.Stdout -match "py")

    $resMatch = Invoke-SuperSearch @($TempRoot, "ALPHA_UNIQUE_STRING")
    Assert-Condition "Exit code 0 when matches are found" ($resMatch.ExitCode -eq 0) "Got ExitCode=$($resMatch.ExitCode)"

    $resNoMatch = Invoke-SuperSearch @($TempRoot, "NON_EXISTENT_PATTERN_XYZ_999")
    Assert-Condition "Exit code 1 when no matches are found" ($resNoMatch.ExitCode -eq 1) "Got ExitCode=$($resNoMatch.ExitCode)"

    $resBadDir = Invoke-SuperSearch @((Join-Path $TempRoot "non_existent_dir_12345"), "needle")
    Assert-Condition "Exit code 2 when target directory does not exist" ($resBadDir.ExitCode -eq 2) "Got ExitCode=$($resBadDir.ExitCode)"

    $resNoArgs = Invoke-SuperSearch @("-d", $TempRoot)
    Assert-Condition "Exit code 2 when search query is missing" ($resNoArgs.ExitCode -eq 2) "Got ExitCode=$($resNoArgs.ExitCode)"

    # --------------------------------------------------------------------------
    # 3. Basic Search, Dual-Target Search & Single-File Target
    # --------------------------------------------------------------------------
    Write-Section "3. Basic Content Search, Dual-Target Search & Single-File Mode"
    $resExplicit = Invoke-SuperSearch @("-d", $TempRoot, "-q", "ALPHA_UNIQUE_STRING", "--json-stdout")
    $jsonExplicit = $resExplicit.Stdout | ConvertFrom-Json
    Assert-Condition "Explicit -d and -q flags find content match" ($jsonExplicit.content_matches -eq 1 -and $jsonExplicit.matches[0].line -eq 4)

    $resDual = Invoke-SuperSearch @($TempRoot, "findme_target_file", "--json-stdout")
    $jsonDual = $resDual.Stdout | ConvertFrom-Json
    Assert-Condition "Dual-target search finds both filename match and content match" ($jsonDual.filename_matches -ge 1 -and $jsonDual.content_matches -eq 1)

    $singleFile = Join-Path $TempRoot "scripts\worker_alpha.py"
    $resSingleFile = Invoke-SuperSearch @($singleFile, "ALPHA_UNIQUE_STRING", "--json-stdout")
    $jsonSingleFile = $resSingleFile.Stdout | ConvertFrom-Json
    Assert-Condition "Single-file target path works directly without directory traversal" ($jsonSingleFile.files_scanned -eq 1 -and $jsonSingleFile.content_matches -eq 1)

    # --------------------------------------------------------------------------
    # 4. Case Sensitivity (-i / --ignore-case)
    # --------------------------------------------------------------------------
    Write-Section "4. Case-Sensitive vs Case-Insensitive Search (-i)"
    $resCaseSens = Invoke-SuperSearch @($TempRoot, "casetest_mixedcase_token", "--json-stdout")
    $jsonCaseSens = $resCaseSens.Stdout | ConvertFrom-Json
    Assert-Condition "Case-sensitive search does not match different casing" ($resCaseSens.ExitCode -eq 1 -and $jsonCaseSens.content_matches -eq 0)

    $resCaseInsens = Invoke-SuperSearch @("-i", $TempRoot, "casetest_mixedcase_token", "--json-stdout")
    $jsonCaseInsens = $resCaseInsens.Stdout | ConvertFrom-Json
    Assert-Condition "Case-insensitive search (-i) matches mixed case content" ($resCaseInsens.ExitCode -eq 0 -and $jsonCaseInsens.content_matches -eq 1)

    # --------------------------------------------------------------------------
    # 5. Inverted Match (-v / --invert-match)
    # --------------------------------------------------------------------------
    Write-Section "5. Inverted Match (-v / --invert-match)"
    $invertTarget = Join-Path $TempRoot "special_module\findme_target_file.txt"
    # File has 4 lines; 2 contain 'needle_'
    $resInvert = Invoke-SuperSearch @("-v", $invertTarget, "needle_", "--json-stdout")
    $jsonInvert = $resInvert.Stdout | ConvertFrom-Json
    Assert-Condition "Inverted match (-v) returns only non-matching lines (2 of 4)" ($jsonInvert.content_matches -eq 2)

    $resInvertRegex = Invoke-SuperSearch @("-v", "-R", $invertTarget, "needle_(alpha|beta)", "--json-stdout")
    $jsonInvertRegex = $resInvertRegex.Stdout | ConvertFrom-Json
    Assert-Condition "Inverted match with Regex (-v -R) returns only non-matching lines (2 of 4)" ($jsonInvertRegex.content_matches -eq 2)

    # --------------------------------------------------------------------------
    # 6. Language Type Presets (-t) & Extension Filters (--ext)
    # --------------------------------------------------------------------------
    Write-Section "6. Language Presets (-t) & Extension Filtering (--ext)"
    $resTypePy = Invoke-SuperSearch @("-t", "py", $TempRoot, "PY_MARKER", "--json-stdout")
    $jsonTypePy = $resTypePy.Stdout | ConvertFrom-Json
    Assert-Condition "Language preset (-t py) scans only Python files" ($jsonTypePy.files_scanned -eq 1 -and $jsonTypePy.content_matches -eq 1)

    $resExtMd = Invoke-SuperSearch @("--ext", ".md", $TempRoot, "START_BLOCK", "--json-stdout")
    $jsonExtMd = $resExtMd.Stdout | ConvertFrom-Json
    Assert-Condition "Extension filter (--ext .md) scans only .md files" ($jsonExtMd.files_scanned -eq 1 -and $jsonExtMd.content_matches -eq 1)

    # --------------------------------------------------------------------------
    # 7. Ignore Hierarchy (.gitignore, .superignore, -x, --no-ignore, --hidden)
    # --------------------------------------------------------------------------
    Write-Section "7. Ignore Rules, Negation (!), Exclusions (-x), --no-ignore & --hidden"
    $resIgnored = Invoke-SuperSearch @($TempRoot, "IGNORED_CONTENT_TOKEN", "--json-stdout")
    Assert-Condition ".gitignore excludes ignored_dir/ and *.log by default" ($resIgnored.ExitCode -eq 1)

    $resNegation = Invoke-SuperSearch @($TempRoot, "IMPORTANT_LOG_KEPT_BY_NEGATION", "--json-stdout")
    $jsonNegation = $resNegation.Stdout | ConvertFrom-Json
    Assert-Condition ".gitignore negation rule (!important.log) keeps negated file" ($resNegation.ExitCode -eq 0 -and $jsonNegation.content_matches -eq 1)

    $resSuperIgnore = Invoke-SuperSearch @($TempRoot, "SUPER_IGNORED_TOKEN", "--json-stdout")
    Assert-Condition ".superignore excludes super_ignored.txt by default" ($resSuperIgnore.ExitCode -eq 1)

    $resNoIgnore = Invoke-SuperSearch @("--no-ignore", $TempRoot, "SUPER_IGNORED_TOKEN", "--json-stdout")
    $jsonNoIgnore = $resNoIgnore.Stdout | ConvertFrom-Json
    Assert-Condition "--no-ignore bypasses .gitignore and .superignore rules" ($resNoIgnore.ExitCode -eq 0 -and $jsonNoIgnore.content_matches -eq 1)

    $resExclude = Invoke-SuperSearch @("-x", "vendor_custom", $TempRoot, "VENDOR_JS_TOKEN", "--json-stdout")
    Assert-Condition "-x / --exclude skips specified custom directories" ($resExclude.ExitCode -eq 1)

    $resHiddenDefault = Invoke-SuperSearch @($TempRoot, "SECRET_HIDDEN_KEY", "--json-stdout")
    Assert-Condition "Hidden dot-files and dot-directories are skipped by default" ($resHiddenDefault.ExitCode -eq 1)

    $resHiddenFlag = Invoke-SuperSearch @("--hidden", $TempRoot, "SECRET_HIDDEN_KEY", "--json-stdout")
    $jsonHiddenFlag = $resHiddenFlag.Stdout | ConvertFrom-Json
    Assert-Condition "--hidden includes dot-files and dot-directories (2 matches)" ($resHiddenFlag.ExitCode -eq 0 -and $jsonHiddenFlag.content_matches -eq 2)

    # --------------------------------------------------------------------------
    # 8. Multi-Pattern Search (-e and -f)
    # --------------------------------------------------------------------------
    Write-Section "8. Multi-Pattern OR Search (-e / --regexp and -f / --file)"
    $resMultiE = Invoke-SuperSearch @("-d", $invertTarget, "-e", "needle_alpha", "-e", "needle_beta", "--json-stdout")
    $jsonMultiE = $resMultiE.Stdout | ConvertFrom-Json
    Assert-Condition "Multiple -e patterns match all specified patterns (2 matches)" ($jsonMultiE.content_matches -eq 2)

    $patternFile = Join-Path ([System.IO.Path]::GetTempPath()) ("ss_patterns_" + [Guid]::NewGuid().ToString("N").Substring(0, 6) + ".list")
    [System.IO.File]::WriteAllText($patternFile, "needle_alpha`nALPHA_UNIQUE_STRING`n", $utf8NoBom)
    $resPatternFile = Invoke-SuperSearch @("-d", $TempRoot, "-f", $patternFile, "--json-stdout")
    $jsonPatternFile = $resPatternFile.Stdout | ConvertFrom-Json
    Assert-Condition "Pattern file (-f) loads and matches multiple patterns across files" ($jsonPatternFile.content_matches -eq 2)
    Remove-Item $patternFile -Force -ErrorAction SilentlyContinue

    # --------------------------------------------------------------------------
    # 9. Multiline Search (-U / --multiline)
    # --------------------------------------------------------------------------
    Write-Section "9. Multiline Search (-U / --multiline)"
    $mdFile = Join-Path $TempRoot "docs\guide.md"
    $resMultiline = Invoke-SuperSearch @("-U", "-d", $mdFile, "-q", 'START_BLOCK\nThis spans across\nmultiple lines\nEND_BLOCK', "--json-stdout")
    $jsonMultiline = $resMultiline.Stdout | ConvertFrom-Json
    Assert-Condition "Multiline search (-U) matches block spanning multiple lines" ($resMultiline.ExitCode -eq 0 -and $jsonMultiline.content_matches -eq 1)

    # --------------------------------------------------------------------------
    # 10. Regular Expression Engine (-R) & Fixed Strings (-F)
    # --------------------------------------------------------------------------
    Write-Section "10. Regular Expression Engine (-R) & Fixed Strings (-F)"
    $resRegexDate = Invoke-SuperSearch @("-R", $TempRoot, "\d{4}-\d{2}-\d{2}", "--json-stdout")
    $jsonRegexDate = $resRegexDate.Stdout | ConvertFrom-Json
    Assert-Condition "Regex (-R) matches ISO date pattern \d{4}-\d{2}-\d{2}" ($jsonRegexDate.content_matches -eq 1 -and $jsonRegexDate.matches[0].line -eq 13)

    $resRegexEmail = Invoke-SuperSearch @("-R", $mdFile, "\w+@\w+\.com", "--json-stdout")
    $jsonRegexEmail = $resRegexEmail.Stdout | ConvertFrom-Json
    Assert-Condition "Regex (-R) matches email pattern \w+@\w+\.com" ($jsonRegexEmail.content_matches -eq 1)

    $resRegexQuant = Invoke-SuperSearch @("-R", $mdFile, "(user|admin)[0-9]{2,4}", "--json-stdout")
    $jsonRegexQuant = $resRegexQuant.Stdout | ConvertFrom-Json
    Assert-Condition "Regex (-R) handles alternation + bounded quantifier {2,4}" ($jsonRegexQuant.content_matches -eq 1)

    # Fixed strings (-F) should treat regex metacharacters literally
    $literalMetaFile = Write-FixtureFile "docs\literal_meta.txt" "Literal regex chars: \d+ [a-z]* (group)`n"
    $resFixed = Invoke-SuperSearch @("-R", "-F", "-d", $literalMetaFile, "-q", "\d+ [a-z]*", "--json-stdout")
    $jsonFixed = $resFixed.Stdout | ConvertFrom-Json
    Assert-Condition "Fixed strings (-F) disables regex parsing and matches literal metacharacters" ($jsonFixed.content_matches -eq 1)

    # --------------------------------------------------------------------------
    # 11. Semantic Code Filtering (--kind)
    # --------------------------------------------------------------------------
    Write-Section "11. Semantic & AST Code Filtering (--kind)"
    $cSample = Join-Path $TempRoot "src\engine_sample.c"

    $resKindAll = Invoke-SuperSearch @($cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonKindAll = $resKindAll.Stdout | ConvertFrom-Json
    Assert-Condition "Unfiltered search in engine_sample.c finds all 5 MAGIC_TOKEN lines" ($jsonKindAll.content_matches -eq 5)

    $resKindComment = Invoke-SuperSearch @("--kind", "comment", $cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonKindComment = $resKindComment.Stdout | ConvertFrom-Json
    Assert-Condition "--kind comment matches only single-line and block comments (2 lines)" ($jsonKindComment.content_matches -eq 2)

    $resKindString = Invoke-SuperSearch @("--kind", "string", $cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonKindString = $resKindString.Stdout | ConvertFrom-Json
    Assert-Condition "--kind string matches only string literals (1 line)" ($jsonKindString.content_matches -eq 1 -and $jsonKindString.matches[0].line -eq 7)

    $resKindCode = Invoke-SuperSearch @("--kind", "code", $cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonKindCode = $resKindCode.Stdout | ConvertFrom-Json
    Assert-Condition "--kind code matches only executable code lines, skipping comments/strings (2 lines)" ($jsonKindCode.content_matches -eq 2)

    $resKindFunc = Invoke-SuperSearch @("--kind", "function", $cSample, "calculate_magic_score", "--json-stdout")
    $jsonKindFunc = $resKindFunc.Stdout | ConvertFrom-Json
    Assert-Condition "--kind function matches only function definition signature (line 6)" ($jsonKindFunc.content_matches -eq 1 -and $jsonKindFunc.matches[0].line -eq 6)

    # --------------------------------------------------------------------------
    # 12. Binary Files (-b), Max File Size (-m), MMap Large File & Threads
    # --------------------------------------------------------------------------
    Write-Section "12. Binary Handling (-b), Max Size (-m), MMap (>64KB) & Threading"
    $resBinSkip = Invoke-SuperSearch @((Join-Path $TempRoot "assets"), "BINARY_SECRET_MARKER", "--json-stdout")
    $jsonBinSkip = $resBinSkip.Stdout | ConvertFrom-Json
    Assert-Condition "Binary files are automatically detected and skipped by default" ($resBinSkip.ExitCode -eq 1 -and $jsonBinSkip.content_matches -eq 0)

    $resBinForce = Invoke-SuperSearch @("-b", (Join-Path $TempRoot "assets"), "BINARY_SECRET_MARKER", "--json-stdout")
    $jsonBinForce = $resBinForce.Stdout | ConvertFrom-Json
    Assert-Condition "-b / --binary forces searching inside binary files" ($resBinForce.ExitCode -eq 0 -and $jsonBinForce.content_matches -eq 1)

    $largeFile = Join-Path $TempRoot "large\big_data.txt"
    $resLarge = Invoke-SuperSearch @($largeFile, "MMAP_LARGE_FILE_NEEDLE_", "--threads", "4", "--json-stdout")
    $jsonLarge = $resLarge.Stdout | ConvertFrom-Json
    Assert-Condition "MMap (>64KB) + SIMD search finds exact line numbers (1500 and 2950)" (
        $jsonLarge.content_matches -eq 2 -and
        $jsonLarge.matches[0].line -eq 1500 -and
        $jsonLarge.matches[1].line -eq 2950
    )

    $resThreads = Invoke-SuperSearch @("--threads", "2", "-m", "50", $TempRoot, "ALPHA_UNIQUE_STRING", "--json-stdout")
    Assert-Condition "--threads N and -m <MB> flags execute multi-threaded search cleanly" ($resThreads.ExitCode -eq 0)

    # --------------------------------------------------------------------------
    # 13. Compressed Archives (-z / --search-zip for .zip and .gz)
    # --------------------------------------------------------------------------
    Write-Section "13. Compressed Archive Search (-z / --search-zip)"
    $resZipOff = Invoke-SuperSearch @((Join-Path $TempRoot "archives"), "ZIP_ARCHIVE_SECRET_NEEDLE", "--json-stdout")
    Assert-Condition "Archives (.zip/.gz) are not searched without -z" ($resZipOff.ExitCode -eq 1)

    $resZipOn = Invoke-SuperSearch @("-z", (Join-Path $TempRoot "archives\bundle.zip"), "ZIP_ARCHIVE_SECRET_NEEDLE", "--json-stdout")
    $jsonZipOn = $resZipOn.Stdout | ConvertFrom-Json
    Assert-Condition "-z searches inside .zip archives in-memory and reports virtual entry path" (
        $resZipOn.ExitCode -eq 0 -and
        $jsonZipOn.content_matches -eq 1 -and
        $jsonZipOn.matches[0].file -match "archived_note\.txt" -and
        $jsonZipOn.matches[0].line -eq 2
    )

    $resGzOn = Invoke-SuperSearch @("-z", (Join-Path $TempRoot "archives\compressed.txt.gz"), "GZIP_STREAM_SECRET_NEEDLE", "--json-stdout")
    $jsonGzOn = $resGzOn.Stdout | ConvertFrom-Json
    Assert-Condition "-z searches inside .gz archives in-memory and reports exact line" (
        $resGzOn.ExitCode -eq 0 -and
        $jsonGzOn.content_matches -eq 1 -and
        $jsonGzOn.matches[0].line -eq 2
    )

    # --------------------------------------------------------------------------
    # 14. Search & Replace Engine (-r, --diff, --in-place, --backup)
    # --------------------------------------------------------------------------
    Write-Section "14. Search & Replace Engine (Dry-Run, Diff, In-Place, Backup, Regex)"
    $replaceFile = Write-FixtureFile "replace_test\sample_replace.txt" @"
Header Line
foo_token = 100;
bar_token = 200;
foo_token = 300;
Footer Line
"@

    # Dry-run replace (default without --in-place)
    $resDryRun = Invoke-SuperSearch @($replaceFile, "foo_token", "-r", "replaced_token", "--json-stdout")
    $jsonDryRun = $resDryRun.Stdout | ConvertFrom-Json
    $diskContentAfterDry = [System.IO.File]::ReadAllText($replaceFile)
    Assert-Condition "Dry-run replace (-r without --in-place) counts replacements without modifying disk" (
        $jsonDryRun.total_replacements -eq 2 -and
        $jsonDryRun.files_modified -eq 0 -and
        $diskContentAfterDry -match "foo_token"
    )

    # Unified diff preview (--diff)
    $resDiff = Invoke-SuperSearch @($replaceFile, "foo_token", "-r", "replaced_token", "--diff")
    Assert-Condition "--diff generates unified diff preview with --- a/, +++ b/, -foo_token, +replaced_token" (
        $resDiff.Stdout -match "--- a/" -and
        $resDiff.Stdout -match "\+\+\+ b/" -and
        $resDiff.Stdout -match "-foo_token = 100;" -and
        $resDiff.Stdout -match "\+replaced_token = 100;"
    )

    # In-place replace with backup (--in-place --backup)
    $resInPlace = Invoke-SuperSearch @($replaceFile, "foo_token", "-r", "replaced_token", "--in-place", "--backup", "--json-stdout")
    $jsonInPlace = $resInPlace.Stdout | ConvertFrom-Json
    $diskContentAfterInPlace = [System.IO.File]::ReadAllText($replaceFile)
    $bakFile = "$replaceFile.bak"
    Assert-Condition "--in-place --backup atomically updates file, creates .bak, and increments backups_created" (
        $jsonInPlace.total_replacements -eq 2 -and
        $jsonInPlace.files_modified -eq 1 -and
        $jsonInPlace.backups_created -eq 1 -and
        (Test-Path $bakFile) -and
        ($diskContentAfterInPlace -match "replaced_token = 100;") -and
        (-not ($diskContentAfterInPlace -match "foo_token"))
    )

    # Regex in-place replace (-R -r --in-place)
    $resRegexReplace = Invoke-SuperSearch @("-R", $replaceFile, "\d{3}", "-r", "999", "--in-place", "--json-stdout")
    $jsonRegexReplace = $resRegexReplace.Stdout | ConvertFrom-Json
    $diskAfterRegexRep = [System.IO.File]::ReadAllText($replaceFile)
    Assert-Condition "Regex in-place replace (-R -r --in-place) replaces all 3-digit numbers with 999" (
        $jsonRegexReplace.total_replacements -eq 3 -and
        ($diskAfterRegexRep -match "replaced_token = 999;") -and
        ($diskAfterRegexRep -match "bar_token = 999;")
    )

    # --------------------------------------------------------------------------
    # 15. AI Agent & Token Economy Features
    # --------------------------------------------------------------------------
    Write-Section "15. AI Agent & Token Economy Features (Context, Porcelain, Plan, Caps, Exports)"
    # Context lines (-C 1)
    $resCtx = Invoke-SuperSearch @("-C", "1", $invertTarget, "needle_alpha", "--json-stdout")
    $jsonCtx = $resCtx.Stdout | ConvertFrom-Json
    Assert-Condition "Context lines (-C 1) attaches lines_before and lines_after arrays in JSON" (
        $jsonCtx.matches[0].lines_before.Count -eq 1 -and
        $jsonCtx.matches[0].lines_after.Count -eq 1 -and
        $jsonCtx.matches[0].lines_before[0] -match "Plain content" -and
        $jsonCtx.matches[0].lines_after[0] -match "needle_beta"
    )

    # Porcelain output (--porcelain)
    $resPorcelain = Invoke-SuperSearch @("--porcelain", $singleFile, "ALPHA_UNIQUE_STRING")
    Assert-Condition "--porcelain emits machine-parseable file and line:col:content format" (
        $resPorcelain.Stdout -match "worker_alpha\.py" -and
        $resPorcelain.Stdout -match "4:13:\s*return `"ALPHA_UNIQUE_STRING`""
    )

    # Plan-only mode (--plan-only --json-stdout)
    $resPlan = Invoke-SuperSearch @("--plan-only", "--json-stdout", $TempRoot, "MAGIC_TOKEN")
    $jsonPlan = $resPlan.Stdout | ConvertFrom-Json
    Assert-Condition "--plan-only --json-stdout returns execution strategy JSON without reading file contents" (
        $resPlan.ExitCode -eq 0 -and
        $jsonPlan.plan_only -eq $true -and
        $jsonPlan.files_discovered -ge 1 -and
        $jsonPlan.recommended_workers -ge 1
    )

    # MCP Schema dump (--mcp-schema)
    $resSchema = Invoke-SuperSearch @("--mcp-schema")
    $jsonSchema = $resSchema.Stdout | ConvertFrom-Json
    Assert-Condition "--mcp-schema outputs valid MCP/OpenAI tool JSON schema" (
        $resSchema.ExitCode -eq 0 -and $jsonSchema.name -eq "supersearch"
    )

    # Max results early termination (--max-results)
    $resMaxRes = Invoke-SuperSearch @("--max-results", "2", $cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonMaxRes = $resMaxRes.Stdout | ConvertFrom-Json
    Assert-Condition "--max-results stops search early and sets truncated=true" (
        $jsonMaxRes.content_matches -eq 2 -and
        $jsonMaxRes.truncated -eq $true
    )

    # Head per file (--head-per-file)
    $resHeadFile = Invoke-SuperSearch @("--head-per-file", "3", $cSample, "MAGIC_TOKEN", "--json-stdout")
    $jsonHeadFile = $resHeadFile.Stdout | ConvertFrom-Json
    Assert-Condition "--head-per-file caps matches per individual file" (
        $jsonHeadFile.content_matches -eq 3
    )

    # Max line length truncation (--max-line-len)
    $minifiedFile = Join-Path $TempRoot "docs\minified.js"
    $resMaxLine = Invoke-SuperSearch @("--max-line-len", "80", $minifiedFile, "LONG_LINE_NEEDLE", "--json-stdout")
    $jsonMaxLine = $resMaxLine.Stdout | ConvertFrom-Json
    Assert-Condition "--max-line-len bounds snippet length to 80 characters centered around match" (
        $jsonMaxLine.matches[0].snippet -match "LONG_LINE_NEEDLE" -and
        $jsonMaxLine.matches[0].snippet.Length -le 80
    )

    # File exports (-o text report and -j JSON report)
    $outTxtPath  = Join-Path ([System.IO.Path]::GetTempPath()) ("ss_export_" + [Guid]::NewGuid().ToString("N").Substring(0, 6) + ".txt")
    $outJsonPath = Join-Path ([System.IO.Path]::GetTempPath()) ("ss_export_" + [Guid]::NewGuid().ToString("N").Substring(0, 6) + ".json")
    $resExport = Invoke-SuperSearch @($TempRoot, "ALPHA_UNIQUE_STRING", "-o", $outTxtPath, "-j", $outJsonPath)
    $exportedJson = Get-Content $outJsonPath -Raw | ConvertFrom-Json
    $exportedTxt  = Get-Content $outTxtPath -Raw
    Assert-Condition "-o and -j export full text and JSON reports to disk" (
        (Test-Path $outTxtPath) -and
        (Test-Path $outJsonPath) -and
        $exportedJson.content_matches -eq 1 -and
        $exportedTxt -match "SUPERSEARCH EXECUTION REPORT"
    )
    Remove-Item $outTxtPath, $outJsonPath -Force -ErrorAction SilentlyContinue

    # --------------------------------------------------------------------------
    # 16. Persistent Binary Index Cache (--cache-build, --cache, --cache-clear)
    # --------------------------------------------------------------------------
    Write-Section "16. Persistent Binary Cache (--cache-build, --cache, --cache-clear) & --watch"
    $resCacheBuild = Invoke-SuperSearch @("--cache-build", "-d", $TempRoot)
    Assert-Condition "--cache-build creates binary index in LOCALAPPDATA\supersearch\cache" (
        $resCacheBuild.ExitCode -eq 0 -and
        $resCacheBuild.Stdout -match "Instant cache built"
    )

    # Search using cache
    $resCacheUse = Invoke-SuperSearch @("--cache", $TempRoot, "ALPHA_UNIQUE_STRING", "--json-stdout")
    $jsonCacheUse = $resCacheUse.Stdout | ConvertFrom-Json
    Assert-Condition "--cache loads index and finds content match accurately" (
        $resCacheUse.ExitCode -eq 0 -and
        $jsonCacheUse.content_matches -eq 1
    )

    # Filename match when loading from cache (regression test for Issue #6 fix)
    $resCacheFilename = Invoke-SuperSearch @("--cache", $TempRoot, "findme_target_file", "--json-stdout")
    $jsonCacheFilename = $resCacheFilename.Stdout | ConvertFrom-Json
    Assert-Condition "--cache evaluates filename/directory matches on cached entries" (
        $jsonCacheFilename.filename_matches -ge 1
    )

    # Filter mismatch invalidation when loading from cache (regression test for Issue #6 fix)
    $resCacheFilterChange = Invoke-SuperSearch @("--cache", "-t", "py", $TempRoot, "MAGIC_TOKEN", "--json-stdout")
    Assert-Condition "--cache invalidates stale cache when filter configuration (-t py) changes" (
        $resCacheFilterChange.ExitCode -eq 1
    )

    # Clear cache
    $resCacheClear = Invoke-SuperSearch @("--cache-clear", "-d", $TempRoot)
    Assert-Condition "--cache-clear removes binary cache from disk" (
        $resCacheClear.ExitCode -eq 0 -and
        $resCacheClear.Stdout -match "Instant cache cleared"
    )

    # Test background watcher (--watch) startup & cache generation
    $watchPsi = New-Object System.Diagnostics.ProcessStartInfo
    $watchPsi.FileName = $ExePath
    $watchPsi.Arguments = "--watch -d `"$TempRoot`""
    $watchPsi.UseShellExecute = $false
    $watchPsi.CreateNoWindow = $true
    $watchProc = [System.Diagnostics.Process]::Start($watchPsi)
    Start-Sleep -Milliseconds 800
    $watchAlive = -not $watchProc.HasExited
    if (-not $watchProc.HasExited) {
        $watchProc.Kill()
        $watchProc.WaitForExit(2000) | Out-Null
    }
    # Verify that the watcher created the cache by clearing it (which returns 'Instant cache cleared' if present)
    $resAfterWatchClear = Invoke-SuperSearch @("--cache-clear", "-d", $TempRoot)
    Assert-Condition "--watch starts filesystem monitor daemon and builds initial cache" (
        $watchAlive -and ($resAfterWatchClear.Stdout -match "Instant cache cleared")
    )

    # --------------------------------------------------------------------------
    # 17. MCP JSON-RPC 2.0 Server Mode (--mcp-serve)
    # --------------------------------------------------------------------------
    Write-Section "17. MCP JSON-RPC 2.0 Server Mode (--mcp-serve)"
    $escapedTempRoot = $TempRoot.Replace('\', '\\')
    $escapedCSample  = $cSample.Replace('\', '\\')
    $mcpInput = @(
        '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
        '{"jsonrpc":"2.0","id":2,"method":"ping"}'
        '{"jsonrpc":"2.0","id":3,"method":"tools/list"}'
        "{`"jsonrpc`":`"2.0`",`"id`":4,`"method`":`"tools/call`",`"params`":{`"name`":`"supersearch`",`"arguments`":{`"directory`":`"$escapedTempRoot`",`"query`":`"ALPHA_UNIQUE_STRING`",`"enclosing_function`":true}}}"
        "{`"jsonrpc`":`"2.0`",`"id`":5,`"method`":`"tools/call`",`"params`":{`"name`":`"supersearch_files`",`"arguments`":{`"directory`":`"$escapedTempRoot`",`"glob`":`"*.py`"}}}"
        "{`"jsonrpc`":`"2.0`",`"id`":6,`"method`":`"tools/call`",`"params`":{`"name`":`"supersearch_outline`",`"arguments`":{`"directory`":`"$escapedCSample`"}}}"
        '{"jsonrpc":"2.0","id":7,"method":"unknown/method"}'
    ) -join "`n"
    $mcpInput += "`n"

    $resMcp = Invoke-SuperSearch -CliArgs @("--mcp-serve") -StdinText $mcpInput
    $mcpLines = $resMcp.Stdout -split "`r?`n" | Where-Object { $_.Trim() -ne "" }

    Assert-Condition "--mcp-serve responds to all 7 JSON-RPC requests cleanly" ($mcpLines.Count -eq 7) "Got $($mcpLines.Count) responses"

    if ($mcpLines.Count -ge 7) {
        $respInit    = $mcpLines[0] | ConvertFrom-Json
        $respPing    = $mcpLines[1] | ConvertFrom-Json
        $respList    = $mcpLines[2] | ConvertFrom-Json
        $respCall    = $mcpLines[3] | ConvertFrom-Json
        $respFiles   = $mcpLines[4] | ConvertFrom-Json
        $respOutline = $mcpLines[5] | ConvertFrom-Json
        $respErr     = $mcpLines[6] | ConvertFrom-Json

        Assert-Condition "MCP initialize returns protocolVersion and serverInfo" (
            $respInit.id -eq 1 -and $respInit.result.serverInfo.name -eq "supersearch"
        )
        Assert-Condition "MCP ping returns valid result object" ($respPing.id -eq 2 -and $null -ne $respPing.result)
        Assert-Condition "MCP tools/list exposes all 5 specialized agent tools" (
            $respList.id -eq 3 -and $respList.result.tools.Count -eq 5 -and $respList.result.tools[0].name -eq "supersearch"
        )
        $innerSearchJson = $respCall.result.content[0].text | ConvertFrom-Json
        Assert-Condition "MCP tools/call executes supersearch with enclosing_function" (
            $respCall.id -eq 4 -and $innerSearchJson.content_matches -eq 1 -and $innerSearchJson.matches[0].enclosing_function -match "def compute_alpha"
        )
        $innerFilesJson = $respFiles.result.content[0].text | ConvertFrom-Json
        Assert-Condition "MCP tools/call executes supersearch_files with glob filter" (
            $respFiles.id -eq 5 -and $innerFilesJson.files_only -eq $true -and $innerFilesJson.returned_files -ge 1
        )
        $innerOutlineJson = $respOutline.result.content[0].text | ConvertFrom-Json
        Assert-Condition "MCP tools/call executes supersearch_outline and extracts symbols" (
            $respOutline.id -eq 6 -and $innerOutlineJson.outline_mode -eq $true -and $innerOutlineJson.content_matches -ge 2
        )
        Assert-Condition "MCP unknown method returns standard JSON-RPC -32601 error" (
            $respErr.id -eq 7 -and $respErr.error.code -eq -32601
        )
    }

    # --------------------------------------------------------------------------
    # 18. AI Coding Agent & ripgrep Drop-In Compatibility (v2.6.0)
    # --------------------------------------------------------------------------
    Write-Section "18. AI Coding Agent & ripgrep Drop-In Compatibility (v2.6.0)"

    # 18.1 Smart positional argument order (<query> <dir>)
    $resRgOrder = Invoke-SuperSearch @("ALPHA_UNIQUE_STRING", $TempRoot, "--json-stdout")
    $jsonRgOrder = $resRgOrder.Stdout | ConvertFrom-Json
    Assert-Condition "Smart positional order (<query> <dir>) auto-detects directory in 2nd position" (
        $resRgOrder.ExitCode -eq 0 -and $jsonRgOrder.content_matches -eq 1
    )

    # 18.2 -l / --files-with-matches
    $resFilesWithMatches = Invoke-SuperSearch @("MAGIC_TOKEN", $TempRoot, "-l")
    $lLines = @($resFilesWithMatches.Stdout -split "`r?`n" | Where-Object { $_.Trim() -ne "" })
    Assert-Condition "-l / --files-with-matches outputs only unique matching file paths without headers" (
        $resFilesWithMatches.ExitCode -eq 0 -and $lLines.Count -eq 1 -and ($lLines[0] -match "engine_sample\.c$")
    )

    # 18.3 --files (fast file list)
    $resListFiles = Invoke-SuperSearch @("--files", $TempRoot, "-g", "*.py")
    $fileLines = @($resListFiles.Stdout -split "`r?`n" | Where-Object { $_.Trim() -ne "" })
    Assert-Condition "--files -g *.py lists matching files without reading contents" (
        $resListFiles.ExitCode -eq 0 -and $fileLines.Count -eq 1 -and $fileLines[0] -match "worker_alpha\.py$"
    )

    # 18.4 -c / --count
    $resCountOnly = Invoke-SuperSearch @("MAGIC_TOKEN", $cSample, "-c")
    Assert-Condition "-c / --count prints <filepath>:<count> per matching file" (
        $resCountOnly.ExitCode -eq 0 -and $resCountOnly.Stdout.Trim() -match "engine_sample\.c:5$"
    )

    # 18.5 -w / --word-regexp
    $resWord = Invoke-SuperSearch @("-w", "needle_alpha", $invertTarget, "--json-stdout")
    $jsonWord = $resWord.Stdout | ConvertFrom-Json
    $resPartialWord = Invoke-SuperSearch @("-w", "needle", $invertTarget, "--json-stdout")
    Assert-Condition "-w / --word-regexp matches whole word 'needle_alpha' and rejects partial 'needle'" (
        $resWord.ExitCode -eq 0 -and $jsonWord.content_matches -eq 1 -and $resPartialWord.ExitCode -eq 1
    )

    # 18.6 -S / --smart-case
    $resSmartLower = Invoke-SuperSearch @("-S", "casetest_mixedcase_token", $singleFile, "--json-stdout")
    $jsonSmartLower = $resSmartLower.Stdout | ConvertFrom-Json
    $resSmartUpper = Invoke-SuperSearch @("-S", "CaseTest_WRONGCASE_Token", $singleFile, "--json-stdout")
    Assert-Condition "-S / --smart-case is case-insensitive for all-lowercase and case-sensitive when uppercase present" (
        $resSmartLower.ExitCode -eq 0 -and $jsonSmartLower.content_matches -eq 1 -and $resSmartUpper.ExitCode -eq 1
    )

    # 18.7 -g / --glob positive and negative (!pattern)
    $resGlob = Invoke-SuperSearch @("--files", $TempRoot, "-g", "*.c", "-g", "!*skip*", "--json-stdout")
    $jsonGlob = $resGlob.Stdout | ConvertFrom-Json
    Assert-Condition "-g / --glob supports positive and negative (!) glob filtering" (
        $resGlob.ExitCode -eq 0 -and $jsonGlob.returned_files -eq 1 -and $jsonGlob.files[0] -match "engine_sample\.c$"
    )

    # 18.8 -W / --enclosing-function
    $resEnclosing = Invoke-SuperSearch @("-W", "return MAGIC_TOKEN", $cSample, "--json-stdout")
    $jsonEnclosing = $resEnclosing.Stdout | ConvertFrom-Json
    Assert-Condition "-W / --enclosing-function attaches enclosing_function signature and enclosing_line" (
        $resEnclosing.ExitCode -eq 0 -and
        $jsonEnclosing.matches[0].enclosing_function -match "int calculate_magic_score" -and
        $jsonEnclosing.matches[0].enclosing_line -gt 0
    )

    # 18.9 --outline / --symbols
    $resOutline = Invoke-SuperSearch @("--outline", $cSample, "--json-stdout")
    $jsonOutline = $resOutline.Stdout | ConvertFrom-Json
    Assert-Condition "--outline extracts function definitions without requiring a search query" (
        $resOutline.ExitCode -eq 0 -and
        $jsonOutline.outline_mode -eq $true -and
        $jsonOutline.content_matches -eq 2
    )

    # 18.10 --max-tokens <N>
    $resMaxTokens = Invoke-SuperSearch @("MAGIC_TOKEN", $cSample, "--max-tokens", "140", "--json-stdout")
    $jsonMaxTokens = $resMaxTokens.Stdout | ConvertFrom-Json
    Assert-Condition "--max-tokens caps JSON output and sets token_limit_reached=true and omitted_matches>0" (
        $resMaxTokens.ExitCode -eq 0 -and
        $jsonMaxTokens.token_limit_reached -eq $true -and
        $jsonMaxTokens.omitted_matches -ge 1 -and
        $jsonMaxTokens.matches.Count -lt $jsonMaxTokens.total_matches
    )

    # 18.11 --mcp-config
    $resMcpCfg = Invoke-SuperSearch @("--mcp-config")
    $jsonMcpCfg = $resMcpCfg.Stdout | ConvertFrom-Json
    Assert-Condition "--mcp-config outputs ready-to-paste mcpServers JSON config" (
        $resMcpCfg.ExitCode -eq 0 -and
        $null -ne $jsonMcpCfg.mcpServers.supersearch -and
        $jsonMcpCfg.mcpServers.supersearch.args[0] -eq "--mcp-serve"
    )

} finally {
    # Clean up temporary fixtures
    if (Test-Path $TempRoot) {
        Remove-Item -Path $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ------------------------------------------------------------------------------
# Final Summary
# ------------------------------------------------------------------------------
Write-Host "`n==================================================================" -ForegroundColor Magenta
Write-Host "  SUPERSEARCH AUTOMATED TEST SUITE SUMMARY" -ForegroundColor Magenta
Write-Host "==================================================================" -ForegroundColor Magenta
Write-Host "  Total Assertions : $Script:TotalTests" -ForegroundColor White
Write-Host "  Passed           : $Script:PassedTests" -ForegroundColor Green
Write-Host "  Failed           : $Script:FailedTests" -ForegroundColor $(if ($Script:FailedTests -eq 0) { "Green" } else { "Red" })
Write-Host "==================================================================`n" -ForegroundColor Magenta

if ($Script:FailedTests -gt 0) {
    Write-Host "Failed Tests:" -ForegroundColor Red
    foreach ($f in $Script:FailureList) {
        Write-Host "  - $f" -ForegroundColor Red
    }
    exit 1
}
exit 0
