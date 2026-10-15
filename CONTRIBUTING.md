# Contributing to SuperSearch

Thank you for your interest in forking, improving, and contributing to **SuperSearch**! This document provides guidelines and architectural context to help you get started quickly.

---

## 1. Design Philosophy

SuperSearch is built around four strict engineering principles:
1. **Zero External Runtime Dependencies**: Written in pure C using native OS primitives and compiler intrinsics. No third-party DLLs or package managers are required to build or run.
2. **Hardware-Accelerated with Graceful Fallback**: Uses 256-bit AVX2 SIMD (`immintrin.h`) when supported by the CPU, and dynamically falls back to an optimized Boyer-Moore-Horspool scalar engine on older/non-AVX2 hardware.
3. **Lock-Free Parallelism**: Work distribution across threads uses atomic operations (`InterlockedExchangeAdd`) and thread-local storage (TLS) to avoid mutex contention.
4. **AI Agent Native**: First-class support for token budgeting (`--max-tokens`), structural outlines (`--outline`), enclosing function attribution (`-W`), `ripgrep`/`fd` flag compatibility, and a built-in Model Context Protocol (`--mcp-serve`) server.

---

## 2. Repository Structure

| Path | Description |
| :--- | :--- |
| `include/` | Header files defining data structures and module interfaces (`supersearch.h`, `engine.h`, `scanner.h`, etc.) |
| `src/main.c` | CLI argument parser, smart positional detection, and top-level orchestration |
| `src/scanner.c` | Ultra-fast directory traversal (`FindFirstFileExW`), glob/type filtering, and LPT execution planner |
| `src/engine.c` | Hybrid I/O (`ReadFile` + `MapViewOfFile`), AVX2 dual-pivot SIMD, Horspool, multiline, and regex matching |
| `src/regex_engine.c` | Self-contained Thompson NFA regular expression compiler and bytecode VM |
| `src/semantic.c` | Lexical state machine (`--kind`), backward enclosing function finder (`-W`), and symbol outline extractor (`--outline`) |
| `src/replace.c` | Dry-run preview, Git unified diff (`--diff`), and atomic in-place file replacement (`--in-place`) |
| `src/cache.c` | Persistent binary directory index (`--cache`) and live filesystem watcher (`--watch`) |
| `src/mcp_server.c` | Built-in 5-tool Model Context Protocol (MCP 2.0) JSON-RPC stdio server (`--mcp-serve`) |
| `src/interactive.c` | Full-screen split-pane live TUI (`-I`) |
| `src/archive.c` & `src/tinf.c` | In-memory `.zip`, `.gz`, and `.tar.gz` decompression (`-z`) |
| `tests/` | Automated E2E test suite (`run_all_tests.ps1`), C unit tests (`test_regex.c`), and competitor benchmarks (`benchmark_suite.ps1`) |
| `.agents/skills/supersearch/` | AI Coding Agent Skill specification (`SKILL.md`), reference docs, and MCP installer script |

---

## 3. Building & Testing

### Prerequisites
- **Windows**: Windows 10/11, **MinGW-w64 GCC** (`gcc`) or **LLVM/Clang** (`clang`), PowerShell 5.1+
- **Linux**: GCC or Clang, `make`, POSIX threads (`-lpthread`)
- **macOS (Intel or Apple Silicon)**: Xcode Command Line Tools (`clang` & `make`)

### Compile the Binary

**On Windows:**
```cmd
build.bat
```

**On Linux & macOS:**
```bash
make
```

### Run the Automated Test Suite
Before submitting any Pull Request, ensure all automated E2E and unit tests pass:

**On Windows (82 E2E assertions + 19 C unit tests):**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_all_tests.ps1
```

**On Linux & macOS (POSIX E2E test suite):**
```bash
chmod +x tests/run_posix_tests.sh
./tests/run_posix_tests.sh
```

### Run the Performance Benchmark Suite (Windows)
If your changes touch `src/engine.c`, `src/scanner.c`, `src/threadpool.c`, or `src/cache.c`, run the benchmark suite to verify there are no performance regressions:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\benchmark_suite.ps1
```

---

## 4. Pull Request Checklist

- [ ] Code compiles cleanly with zero warnings under `-Wall -Wextra` on Windows, Linux, and macOS.
- [ ] All comments, CLI help text, and documentation are in English.
- [ ] New features include corresponding test cases in `tests/run_all_tests.ps1` and `tests/run_posix_tests.sh`.
- [ ] All automated tests pass with `0` failures.
