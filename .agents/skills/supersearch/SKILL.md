---
name: supersearch
description: >-
  Ultra-fast parallel file, code, and symbol search engine with AVX2 SIMD acceleration, enclosing function
  attribution (-W), structural code outlines (--outline), semantic filtering (--kind), safe multi-file search
  and replace (-r/--diff/--in-place), LLM token budgeting (--max-tokens), and ripgrep/fd drop-in compatibility.
  Use this skill whenever searching for code patterns, symbols, function definitions, file names, repository
  structure, or performing bulk search-and-replace across a codebase.
---

# SuperSearch — AI Coding Agent Skill (`v2.6.0`)

**Binary Resolution:**
- If `supersearch.exe` is on `PATH` (or `$env:SUPERSEARCH_BIN` is set), invoke `supersearch.exe` directly.
- When working inside the SuperSearch repository, invoke `.\bin\supersearch.exe` (if missing, run `.\build.bat` once to compile it).

SuperSearch is a native C, 256-bit AVX2 SIMD-accelerated parallel search and refactoring engine designed specifically for AI Coding Agents. It eliminates unnecessary full-file reads (`view_file`) by returning **enclosing function signatures (`-W`)**, **file symbol outlines (`--outline`)**, and **strict token-capped JSON/porcelain outputs (`--max-tokens`)**.

---

## 1. Agent Decision Tree (Choose the Right Mode)

Before running a search, pick the most token-efficient workflow for your goal:

| Agent Goal | Recommended Command | Why |
| :--- | :--- | :--- |
| **Understand a file or module's structure** before reading it | `supersearch.exe --outline <path> --porcelain` | Extracts all function, class, struct, and interface definitions with line numbers in one call. |
| **Find where a symbol/keyword is used** and know which function it belongs to | `supersearch.exe "<query>" <dir> -S -W --porcelain` | Attaches `@@ <line>: <signature> @@` to every match so you immediately know the enclosing function. |
| **Find exact whole-word identifier references** (e.g. variable or function name) | `supersearch.exe "<identifier>" <dir> -w -W --porcelain` | `-w` enforces word boundaries (`\b...\b`), avoiding substring noise. |
| **Discover files by name, extension, or glob** (`fd` / `rg --files` replacement) | `supersearch.exe --files <dir> -g "*.ts" -g "!*test*"` | Traverses directory tree in parallel without reading file contents. |
| **List only file paths containing a match** | `supersearch.exe "<query>" <dir> -l` | Stops scanning each file immediately after its first match (`max_matches_per_file = 1`). |
| **Filter by semantic code context** (skip comments/strings or find only definitions) | `supersearch.exe "<query>" <dir> --kind <code\|function\|comment\|string> --porcelain` | Avoids false positives inside comments or string literals. |
| **Preview or apply bulk refactoring** across multiple files | `supersearch.exe "<old>" <dir> -r "<new>" --diff` | Generates a standard Git unified diff preview (`--diff`) or applies atomically (`--in-place`). |
| **Inspect unknown/huge repo size** before running a broad query | `supersearch.exe <dir> --plan-only --json-stdout` | Returns file counts, byte volume, and size distribution in `<2 ms`. |

---

## 2. Core Workflows & Command Recipes

### Workflow A: Structural Code Exploration (`--outline` + `-W`)
Instead of reading an entire 1,500-line file to locate a function:
1. **Step 1 — Get the symbol map of the file or directory:**
   ```powershell
   supersearch.exe --outline <target_file_or_dir> --porcelain
   ```
2. **Step 2 — Search for specific logic with enclosing function attribution (`-W`) and context (`-C 2`):**
   ```powershell
   supersearch.exe "<query>" <target_dir> -S -W -C 2 --max-tokens 2000 --json-stdout
   ```

### Workflow B: High-Precision Code Search (`ripgrep` Drop-In Compatible)
SuperSearch accepts both `<dir> <query>` and `rg`-style `<query> <dir>` positional orders:
```powershell
# Smart-case (-S), whole-word (-w), glob filters (-g), and enclosing function (-W):
supersearch.exe "create_plan" src -S -w -W -g "*.c" -g "!*test*" --porcelain

# Multi-pattern OR search (-e) with language preset (-t):
supersearch.exe src -t c -e "malloc" -e "calloc" --kind code --porcelain

# Regular expression search (-R) across newline boundaries (-U):
supersearch.exe src -R -U "struct\s+\w+\s*\{[\s\S]*?\}" --json-stdout
```

### Workflow C: Safe Multi-File Search & Replace
Always run a dry-run diff preview (`--diff`) before applying in-place changes (`--in-place`):
1. **Step 1 — Preview modifications as a unified diff:**
   ```powershell
   supersearch.exe "<target_pattern>" <dir> -r "<replacement>" --diff
   ```
2. **Step 2 — Apply atomically to disk (optionally with `--backup`):**
   ```powershell
   supersearch.exe "<target_pattern>" <dir> -r "<replacement>" --in-place
   ```

### Workflow D: Sub-Millisecond Repeated Searches (`--cache`)
When performing multiple searches on a large repository (e.g., `>5,000` files), enable `--cache`:
```powershell
supersearch.exe "<query>" <dir> --cache -W --porcelain
```

---

## 3. Token Economy Best Practices for Agents

1. **Always prefer `--porcelain` or `--json-stdout`**:
   - `--porcelain` strips ASCII banners and ANSI colors, emitting compact `file:line:col:text` lines (and `@@ line: func @@` when `-W` is active).
   - `--json-stdout` emits a single valid JSON object with full metadata (`enclosing_function`, `enclosing_line`, `lines_before`, `lines_after`, `token_limit_reached`, `omitted_matches`).
2. **Protect your context window on broad queries**:
   - Pass `--max-tokens 2000` or `--max-results 50` (and `--head-per-file 10`) so noisy matches never flood your context window.
3. **Interpret Exit Codes**:
   - `0`: One or more matches found (or `--files` / `--plan-only` succeeded).
   - `1`: Search completed cleanly with `0` matches found (not an error).
   - `2`: Invalid CLI arguments or target path does not exist.

---

## 4. References & Helper Scripts

- **Detailed CLI Flag & MCP 2.0 JSON Schema Reference:** See [cli-and-mcp-reference.md](./references/cli-and-mcp-reference.md) when you need the full list of CLI flags, JSON response field definitions, or MCP JSON-RPC 2.0 tool schemas.
- **MCP & Global Skill Installer Script:** Run [install-mcp.ps1](./scripts/install-mcp.ps1) to register `supersearch` as a global skill, optionally add it to `PATH`, or generate MCP server configurations dynamically for the current machine.
