# SuperSearch v2.6.0 — Complete CLI & MCP 2.0 Reference

This reference document details all command-line flags, JSON output schemas, and Model Context Protocol (MCP 2.0) tool specifications for `supersearch.exe`.

---

## 1. Complete CLI Flags Reference

### Positional Arguments
- `supersearch.exe <directory> <query> [options]`
- `supersearch.exe <query> [directory] [options]` (`ripgrep` compatible; defaults `<directory>` to `.` if omitted)

### Search & Filtering Options
| Flag | Description |
| :--- | :--- |
| `-d, --dir <path>` | Explicit target directory or file path |
| `-q, --query <str>` | Explicit search query or regular expression |
| `-i, --ignore-case` | Case-insensitive search |
| `-S, --smart-case` | Case-insensitive unless query contains uppercase ASCII letters |
| `-w, --word-regexp` | Match whole words only (`\b...\b`) |
| `-v, --invert-match` | Select non-matching lines |
| `-R, --regex` | Enable regular expression engine (Thompson NFA bytecode VM) |
| `-F, --fixed-strings` | Treat patterns strictly as literal strings (disable auto-regex detection) |
| `-U, --multiline` | Match patterns spanning across `\r\n` and `\n` line boundaries |
| `-e, --regexp <pat>` | Add search pattern (can be specified multiple times for logical OR) |
| `-f, --file <path>` | Load newline-separated search patterns from file |
| `-g, --glob <glob>` | Include (`*.c`, `src/**`) or exclude (`!*.min.js`, `!*test*`) files by glob |
| `-t, --type <type>` | Filter by language preset (`c`, `cpp`, `cs`, `rust`, `py`, `js`, `ts`, `web`, `go`, `java`, `sql`, `json`, `yaml`, `md`, `ps1`, etc.) |
| `--type-list` | Print all 26 supported language presets and extensions |
| `--ext <.ext1,.ext2>` | Comma-separated file extension filter |
| `-x, --exclude <dirs>` | Comma-separated directory names to exclude (default: `.git,.vs,node_modules,bin,obj`) |
| `--no-ignore` | Bypass `.gitignore` and `.superignore` rules |
| `--hidden` | Include hidden dot-files and dot-directories |
| `-b, --binary` | Search binary files (default: text files only) |
| `-z, --search-zip` | Search inside `.zip`, `.gz`, and `.tar.gz` archives in memory |
| `-m, --max-size <MB>` | Skip files larger than `<MB>` megabytes |
| `--threads <N>` | Override worker thread count (default: auto-detect logical cores) |

### Semantic & Token Economy Options
| Flag | Description |
| :--- | :--- |
| `-W, --enclosing-function` | Scan backward from each match to attach the enclosing function/class/struct signature (`enclosing_function`) and line number (`enclosing_line`) |
| `--outline, --symbols` | Extract all function, method, class, struct, interface, and enum definitions without a query |
| `--kind <kind>` | Filter matches by AST/lexical scope: `code`, `function`, `comment`, or `string` |
| `-l, --files-with-matches` | Print only unique file paths containing matches (stops after first match per file) |
| `--files` | Print all candidate file paths without reading file contents (`fd` / `rg --files`) |
| `-c, --count` | Print `<filepath>:<count>` for each matching file |
| `--max-tokens <N>` | Cap output at approximately `N` LLM tokens (`~N*4` bytes); sets `token_limit_reached: true` and `omitted_matches` |
| `--max-results <N>` | Hard cap on total matches; atomically halts all worker threads once reached |
| `--head-per-file <N>` | Maximum matches returned per individual file (`default: 500`) |
| `--max-line-len <N>` | Maximum characters per snippet line (`default: 250`) |
| `-C, --context <N>` | Include `N` lines of code before and after each match |
| `-B, --before <N>` | Include `N` lines of code before each match |
| `-A, --after <N>` | Include `N` lines of code after each match |
| `--json-stdout` | Emit structured JSON output to `stdout` |
| `--porcelain, --quiet` | Emit compact machine-readable lines without headers or ANSI colors |
| `--plan-only, --dry-run` | Inspect directory and output execution plan without scanning file contents |

### Refactoring & Cache Options
| Flag | Description |
| :--- | :--- |
| `-r, --replace <str>` | Replacement string for matched patterns |
| `--diff` | Print replacement preview in Git unified diff format |
| `--in-place` | Atomically write replacements to disk via temporary file + `MoveFileExW` |
| `--backup` | Save `.bak` copy of each file before `--in-place` modification |
| `--cache` | Use persistent binary directory cache (`%LOCALAPPDATA%\supersearch\cache`) |
| `--cache-build` | Force rebuild persistent binary directory cache |
| `--cache-clear` | Delete persistent binary directory cache for target directory |
| `--watch` | Start background `ReadDirectoryChangesW` daemon to keep cache fresh |

---

## 2. JSON Output Schema (`--json-stdout`)

```json
{
  "query": "semantic_find_enclosing_function",
  "directory": "src",
  "ignore_case": false,
  "invert_match": false,
  "multiline": false,
  "regex": false,
  "search_zip": false,
  "replace_mode": false,
  "total_matches": 1,
  "filename_matches": 0,
  "content_matches": 1,
  "files_with_matches": 1,
  "truncated": false,
  "token_limit_reached": false,
  "omitted_matches": 0,
  "files_scanned": 15,
  "bytes_scanned": 312450,
  "inspection_time_ms": 0.45,
  "search_time_ms": 1.12,
  "total_time_ms": 1.65,
  "throughput_mb_s": 266.08,
  "workers": 15,
  "matches": [
    {
      "file": "src\\engine.c",
      "is_filename": false,
      "line": 243,
      "column": 33,
      "snippet": "        match->enclosing_func = semantic_find_enclosing_function(",
      "enclosing_function": "static bool add_thread_match(",
      "enclosing_line": 193,
      "lines_before": [
        "    if (options && options->enclosing_function && g_tls_line_cache.data_ptr != NULL) {",
        "        int64_t enc_line = 0;"
      ],
      "lines_after": [
        "            g_tls_line_cache.data_ptr,",
        "            g_tls_line_cache.data_len,"
      ]
    }
  ]
}
```

---

## 3. MCP 2.0 Server Tools (`--mcp-serve`)

When launched with `supersearch.exe --mcp-serve`, SuperSearch exposes 5 tools over JSON-RPC 2.0 `stdio`:

1. **`supersearch`**:
   - Parameters: `query` (required), `directory`, `ignore_case`, `smart_case`, `word_regexp`, `invert_match`, `regex`, `fixed_strings`, `multiline`, `search_zip`, `kind`, `enclosing_function`, `files_with_matches`, `count_only`, `glob`, `extensions`, `type`, `exclude`, `max_results`, `max_tokens`, `context`, `use_cache`.
2. **`supersearch_files`**:
   - Parameters: `directory`, `glob`, `extensions`, `type`, `exclude`, `hidden`, `no_ignore`, `max_results`.
3. **`supersearch_outline`**:
   - Parameters: `directory` (required), `glob`, `extensions`, `type`, `max_results`, `max_tokens`.
4. **`supersearch_replace`**:
   - Parameters: `directory` (required), `query` (required), `replace` (required), `in_place`, `backup`, `regex`, `ignore_case`, `word_regexp`, `glob`, `extensions`, `type`.
5. **`supersearch_plan`**:
   - Parameters: `directory`, `query`, `glob`, `extensions`, `type`.
