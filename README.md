# SuperSearch — Ultra-Fast Parallel File & Code Search Engine

[![Version](https://img.shields.io/badge/version-2.6.0-blue.svg)](https://github.com/aliemreerdem/super-search/releases)
[![Releases](https://img.shields.io/github/v/release/aliemreerdem/super-search?include_prereleases&label=Download%20Release)](https://github.com/aliemreerdem/super-search/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](./LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-0078D6.svg)](https://github.com/aliemreerdem/super-search/releases)
[![SIMD](https://img.shields.io/badge/SIMD-AVX2%20256--bit%20%7C%20ARM%20NEON%20128--bit%20%7C%20Scalar-orange.svg)](#)
[![AI Agent Ready](https://img.shields.io/badge/AI%20Agent-MCP%202.0%20%2B%20Skill-purple.svg)](./.agents/skills/supersearch/SKILL.md)

Developed from scratch in pure C with **zero external runtime dependencies**, **SuperSearch (`v2.6.0`)** harnesses the full power of modern multi-core processors across **Windows**, **Linux**, and **macOS** (256-bit x86 AVX2 SIMD, 128-bit ARM NEON SIMD for Apple Silicon / ARM64, lock-free Win32 & POSIX `pthread` worker pools, and zero-copy memory-mapped files). Before executing a search, it inspects the target directory, builds an optimal **Execution Plan**, and executes the search in parallel with full **AI Coding Agent**, **`ripgrep (rg)`**, **`fd`**, and **Model Context Protocol (MCP 2.0)** compatibility.

---

## 🚀 Key Features

1. **Pre-Search Inspection & Execution Planning**:
   - Traverses the directory tree in microseconds using `FindFirstFileExW` on Windows or `opendir`/`readdir`/`lstat` on Linux & macOS.
   - Categorizes files by size (`Tiny`, `Small`, `Medium`, `Large`, `Huge`).
   - Dynamically selects the optimal worker thread count and dispatch chunk size based on CPU core topology and file distribution.
   - Applies **LPT (Longest Processing Time First)** scheduling so large files are processed first, eliminating tail latency.

2. **Dual-Target Search (Filenames + Content)**:
   - Simultaneously matches file/directory names and file contents in a single pass, or operates in dedicated file-listing mode (`--files` / `-l`).

3. **Dynamic CPUID Dispatch & Multi-Architecture SIMD Acceleration (AVX2 + ARM NEON + Scalar)**:
   - Queries CPU hardware capabilities at startup (`__builtin_cpu_supports` / `__cpuidex` / `IsProcessorFeaturePresent` on x86_64, and native NEON on ARM64).
   - **On x86_64 AVX2 CPUs**: Uses 256-bit dual-pivot AVX2 SIMD (`_mm256_cmpeq_epi8`, `_mm256_movemask_epi8`, `_tzcnt_u32`) to scan 32 bytes per instruction cycle.
   - **On Apple Silicon (M1/M2/M3/M4) & Linux ARM64**: Uses 128-bit dual-pivot ARM NEON SIMD (`vceqq_u8`, `vandq_u8`, `vmaxvq_u8`) to scan 16 bytes per cycle.
   - **On Older / Non-SIMD CPUs**: Automatically falls back to an optimized Boyer-Moore-Horspool scalar engine without crashing (`SIGILL`).

4. **Hybrid Zero-Copy I/O Architecture**:
   - **Small Files (`< 64 KB`)**: Direct single-syscall `ReadFile` (Windows) or `open`/`read` (POSIX) into thread-local buffers (zero `mmap` setup overhead).
   - **Medium & Large Files (`64 KB – 16 MB+`)**: Zero-copy memory-mapped I/O (`CreateFileMappingW` + `MapViewOfFile` on Windows, `mmap` + `madvise(MADV_SEQUENTIAL)` on Linux/macOS) directly from the OS page cache.
   - **Huge Files (`> 32 MB`)**: Windowed memory mapping with pattern-length boundary overlap to prevent address-space exhaustion.

5. **Lock-Free Parallel Work Stealing**:
   - Atomic work distribution via `InterlockedExchangeAdd` / `__atomic_fetch_add` with zero mutex contention across up to 64 threads.
   - Each worker accumulates matches in thread-local storage before lock-free aggregation.

6. **AI Coding Agent & `ripgrep (rg)` Drop-In Compatibility**:
   - Flexible positional arguments: supports both `<directory> <query>`, `rg`-style `<query> <directory>`, and `<query>` (defaults to `.`).
   - Full support for `-l` (`--files-with-matches`), `--files`, `-c` (`--count`), `-w` (`--word-regexp`), `-S` (`--smart-case`), and `-g` (`--glob <pattern>` / `!<pattern>`).
   - **Enclosing Function Attribution (`-W` / `--enclosing-function`)**: Automatically identifies the enclosing function, method, class, or struct signature and line number for every match.
   - **Code Outline / Symbol Map (`--outline` / `--symbols`)**: Extracts all function, class, struct, interface, and enum definitions from a file or directory without a search query.
   - **LLM Token Budget Protection (`--max-tokens <N>` & `--max-results <N>`)**: Prevents context window overflow by halting or truncating output when the token budget is reached.

---

## 📦 Download Pre-Built Binaries (Releases)

You can download pre-compiled binaries for your platform directly from the [**GitHub Releases Page**](https://github.com/aliemreerdem/super-search/releases):

| Operating System | Architecture | SIMD Engine | Direct Download |
| :--- | :--- | :--- | :--- |
| **Windows 10 / 11 / Server** | `x86_64` (64-bit) | 256-bit AVX2 + Scalar | [`supersearch-windows-x64.zip`](https://github.com/aliemreerdem/super-search/releases/latest/download/supersearch-windows-x64.zip) |
| **Linux** | `x86_64` (64-bit) | 256-bit AVX2 + Scalar | [`supersearch-linux-x64.tar.gz`](https://github.com/aliemreerdem/super-search/releases/latest/download/supersearch-linux-x64.tar.gz) |
| **Linux** | `ARM64` (`aarch64`) | 128-bit ARM NEON + Scalar | [`supersearch-linux-arm64.tar.gz`](https://github.com/aliemreerdem/super-search/releases/latest/download/supersearch-linux-arm64.tar.gz) |
| **macOS (Apple Silicon M1–M4)** | `ARM64` (`arm64`) | 128-bit ARM NEON + Scalar | [`supersearch-macos-arm64.tar.gz`](https://github.com/aliemreerdem/super-search/releases/latest/download/supersearch-macos-arm64.tar.gz) |
| **macOS (Intel)** | `x86_64` (64-bit) | 256-bit AVX2 + Scalar | [`supersearch-macos-x64.tar.gz`](https://github.com/aliemreerdem/super-search/releases/latest/download/supersearch-macos-x64.tar.gz) |

### One-Line Package Manager Install

**Windows ([Scoop](https://scoop.sh/)):**
```powershell
scoop install https://raw.githubusercontent.com/aliemreerdem/super-search/main/packaging/scoop/supersearch.json
```

**macOS & Linux ([Homebrew](https://brew.sh/)):**
```bash
brew install --formula https://raw.githubusercontent.com/aliemreerdem/super-search/main/packaging/homebrew/supersearch.rb
```

---

## 🛠️ Build from Source & Installation

### 1. Build from Source

**On Windows (PowerShell / CMD):**
Run `build.bat` (auto-detects **MinGW-w64 GCC** or **LLVM/Clang**):
```cmd
git clone https://github.com/aliemreerdem/super-search.git
cd super-search
build.bat
```

**On Linux & macOS (Bash / Zsh):**
Run `make` (auto-detects `gcc` or `clang`, plus x86_64 AVX2 or ARM64 NEON flags):
```bash
git clone https://github.com/aliemreerdem/super-search.git
cd super-search
make
```

### 2. Install AI Agent Skill & Add to `PATH` (Optional)

**On Windows:**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .agents\skills\supersearch\scripts\install-mcp.ps1 -AddToPath
```

**On Linux & macOS:**
```bash
chmod +x .agents/skills/supersearch/scripts/install-mcp.sh
./.agents/skills/supersearch/scripts/install-mcp.sh --add-to-path
```

---

## 📖 CLI Usage Guide

### Basic Syntax

```cmd
supersearch.exe <directory> <query> [options]
supersearch.exe <query> [directory] [options]   (ripgrep / grep compatible order)
supersearch.exe -d <directory> -q <query> [options]
```

### Command-Line Options

| Short | Long Option | Description |
|---|---|---|
| `-d` | `--dir <path>` | Target directory or single file to scan |
| `-q` | `--query <phrase>` | Search keyword, phrase, or regular expression |
| `-i` | `--ignore-case` | Case-insensitive search |
| `-S` | `--smart-case` | Case-insensitive unless the query contains uppercase characters (`rg` compatible) |
| `-w` | `--word-regexp` | Match whole words only (`\b...\b`) |
| `-v` | `--invert-match` | Invert match: select non-matching lines |
| `-l` | `--files-with-matches` | Print only paths of files with matches (stops after 1st match per file) |
| | `--files` | List all candidate files without searching contents (`fd` / `rg --files` equivalent) |
| `-c` | `--count` | Print match count per file in `<file>:<count>` format |
| `-g` | `--glob <pattern>` | Include/exclude files matching glob (e.g., `-g "*.c" -g "!*test*"`) |
| `-t` | `--type <type>` | Predefined language filter (`c, cpp, cs, py, js, ts, rust, go, java, web, sql...`) |
| | `--type-list` | List all supported language presets and extensions |
| `-e` | `--regexp <pattern>` | Specify multiple search patterns combined with logical OR (`-e p1 -e p2`) |
| `-f` | `--file <file>` | Read newline-separated search patterns from a file |
| `-R` | `--regex` | Enable the Thompson NFA bytecode regular expression engine |
| `-F` | `--fixed-strings` | Treat patterns strictly as literal fixed strings |
| `-z` | `--search-zip` | Search inside compressed archives in memory (`.zip, .gz, .tar.gz`) |
| `-U` | `--multiline` | Enable multiline matching across `\r\n` and `\n` boundaries |
| `-r` | `--replace <text>`| Replacement text for matched patterns |
| | `--in-place` | Apply replacements atomically to files on disk (Default: dry-run preview) |
| | `--backup` | Create `.bak` backup copies before modifying files |
| | `--diff` | Output changes in Git unified diff patch format |
| | `--threads <num>` | Number of parallel worker threads (Default: auto-detect logical cores) |
| | `--ext <.c,.h,...>` | Comma-separated file extension filter |
| `-x` | `--exclude <dirs>` | Comma-separated directories to exclude (Default: `.git,.vs,node_modules,bin,obj`) |
| | `--no-ignore` | Do not respect `.gitignore` and `.superignore` rules |
| | `--hidden` | Search hidden dot-files and dot-directories |
| `-b` | `--binary` | Search binary files as well (Default: text files only) |
| `-m` | `--max-size <MB>` | Maximum file size to scan in MB |
| `-o` | `--output <file>` | Save detailed search report to a plain text file |
| `-j` | `--json <file>` | Export structured search report to a JSON file |
| | `--no-color` | Disable ANSI colored terminal output |
| `-h` | `--help` | Display help message |
| `-W` | `--enclosing-function` | Attach the enclosing function/class signature and line number to each match |
| | `--outline`, `--symbols` | Extract all function, struct, and class definitions without a query |
| | `--max-tokens <N>` | Cap output at approximately `N` LLM tokens (`~N*4` bytes) |
| | `--json-stdout` | Output pure JSON directly to `stdout` |
| | `--porcelain` | Output raw `file:line:col:content` lines without banners or colors |
| | `--plan-only` | Return directory profile, file counts, and execution plan without scanning contents |
| | `--max-results <N>` | Global match cap (atomically halts all worker threads once reached) |
| | `--head-per-file <N>` | Maximum matches returned per individual file |
| `-C` | `--context <N>` | Include `N` lines of surrounding context before and after each match |
| `-B` | `--before <N>` | Include `N` lines of context before each match |
| `-A` | `--after <N>` | Include `N` lines of context after each match |
| | `--max-line-len <N>`| Maximum characters per snippet line (Default: `250`) |
| | `--kind <kind>` | Semantic code filter: `comment`, `string`, `code`, or `function` |
| | `--cache` | Use persistent binary directory index cache for sub-millisecond discovery |
| | `--cache-build` | Force rebuild persistent directory index cache |
| | `--cache-clear` | Delete stored cache for target directory |
| | `--watch` | Run background directory watcher daemon via `ReadDirectoryChangesW` |
| `-I` | `--interactive` | Launch full-screen live interactive TUI mode |
| | `--mcp-schema` | Print Model Context Protocol (MCP) / OpenAI tool schema JSON |
| | `--mcp-serve` | Start persistent 5-tool MCP stdio JSON-RPC 2.0 server |
| | `--mcp-config` | Print ready-to-paste `mcpServers` JSON configuration |

---

## 💡 Examples

```cmd
# 1. Search a phrase across a project directory:
bin\supersearch.exe c:\Code\MyProject "database connection"

# 2. ripgrep-compatible argument order with smart-case, enclosing function, and JSON output:
bin\supersearch.exe "TODO" src -S -W --json-stdout

# 3. Extract structural symbol outline of a file or directory:
bin\supersearch.exe --outline src --json-stdout

# 4. Fast file listing with positive and negative glob filters:
bin\supersearch.exe --files -g "*.c" -g "!*test*"

# 5. Preview search & replace as a Git unified diff:
bin\supersearch.exe src "old_api" -r "new_api" --diff

# 6. Atomic in-place refactoring with .bak backups:
bin\supersearch.exe src "old_api" -r "new_api" --in-place --backup
```

---

## 🖥️ Live Interactive TUI Mode (`-I, --interactive`)

Launch a zero-flicker split-pane terminal UI that updates search results in milliseconds as you type and previews surrounding code context on the right pane:

```cmd
bin\supersearch.exe -I
bin\supersearch.exe src "threadpool" -I
```

- **`[Up/Down]` / `[PgUp/PgDn]`**: Navigate matches with live right-pane file preview.
- **`[TAB]`**: Toggle Regular Expression mode (`On` / `Off`).
- **`[F2]`**: Toggle Case Sensitivity (`Sensitive` / `Insensitive`).
- **`[F4]`**: Toggle Compressed Archive scanning (`.zip`, `.gz`).
- **`[ENTER]`**: Emit selected match in `file:line:col:text` format to `stdout` and exit.
- **`[ESC]` / `[Ctrl+C]`**: Restore terminal screen buffer and exit.

---

## 🤖 Model Context Protocol (MCP 2.0) Server (`--mcp-serve`)

SuperSearch includes a built-in zero-disk-I/O MCP JSON-RPC 2.0 server exposing **5 specialized tools** for Claude Code, Cursor, Windsurf, Cline, and Antigravity:

1. `supersearch`: Full content, regex, semantic (`kind`), `enclosing_function`, and token-budgeted search.
2. `supersearch_files`: Fast file discovery (`--files`) with glob and language filters.
3. `supersearch_outline`: Structural code outline (`--outline`) extracting functions, classes, and structs.
4. `supersearch_replace`: Safe dry-run preview or atomic in-place search & replace.
5. `supersearch_plan`: Pre-search repository inspection and execution plan estimator.

Generate your MCP client configuration automatically with:

```cmd
bin\supersearch.exe --mcp-config
```

### Standard Exit Codes
- `0`: One or more matches found (or metadata/plan command succeeded).
- `1`: Search completed cleanly with `0` matches found.
- `2`: Invalid CLI arguments or inaccessible target path.

---

## 🧪 Automated Testing & Benchmarks

Run the comprehensive automated test suite (**82 E2E assertions + 19 C unit tests**):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_all_tests.ps1
```

Run the 10-scenario head-to-head benchmark suite against `ripgrep`, `git grep`, `GNU grep`, `fd`, and `findstr`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\benchmark_suite.ps1
```

---

## 🤝 Contributing & Forking

Contributions, issues, and forks are welcome! Please read [CONTRIBUTING.md](./CONTRIBUTING.md) for details on the codebase architecture, coding conventions, and pull request checklist.

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](./LICENSE) file for details.

