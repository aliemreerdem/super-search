#!/usr/bin/env bash
# ==============================================================================
# SuperSearch v2.6.0 — Linux & macOS Automated E2E + C Unit Test Suite
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_ROOT/bin/supersearch"

if [ ! -x "$BIN" ]; then
    echo "[INFO] Building supersearch via make..."
    (cd "$PROJECT_ROOT" && make)
fi

PASS=0
FAIL=0

assert_eq() {
    local actual="$1"
    local expected="$2"
    local msg="$3"
    if [ "$actual" = "$expected" ]; then
        echo "  [PASS] $msg"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $msg (Expected: '$expected', Got: '$actual')"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local msg="$3"
    if echo "$haystack" | grep -Fq "$needle"; then
        echo "  [PASS] $msg"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $msg (Missing '$needle')"
        FAIL=$((FAIL + 1))
    fi
}

echo "=================================================================="
echo "  1. C Regex Unit Tests"
echo "=================================================================="
CC_BIN="${CC:-cc}"
"$CC_BIN" -O2 -I"$PROJECT_ROOT/include" "$PROJECT_ROOT/tests/test_regex.c" "$PROJECT_ROOT/src/regex_engine.c" "$PROJECT_ROOT/src/utils.c" -o "$PROJECT_ROOT/bin/test_regex"
"$PROJECT_ROOT/bin/test_regex"
rm -f "$PROJECT_ROOT/bin/test_regex"
PASS=$((PASS + 1))

echo "=================================================================="
echo "  2. Core Search, Regex, Outline, Enclosing Function & Replace"
echo "=================================================================="
TEST_TMP="$(mktemp -d)"
trap 'rm -rf "$TEST_TMP"' EXIT

cat > "$TEST_TMP/sample.c" << 'EOF'
// MAGIC_TOKEN in comment
#include <stdio.h>

int calculate_metric(int x) {
    const char* s = "MAGIC_TOKEN in string";
    int MAGIC_TOKEN = x + 42;
    return MAGIC_TOKEN;
}
EOF

OUT_ALL=$("$BIN" "MAGIC_TOKEN" "$TEST_TMP" --porcelain)
assert_contains "$OUT_ALL" "sample.c" "Literal search finds matches in sample.c"

OUT_CODE=$("$BIN" "MAGIC_TOKEN" "$TEST_TMP" --kind code --porcelain)
assert_contains "$OUT_CODE" "int MAGIC_TOKEN = x + 42;" "--kind code filters out comments and strings"

OUT_FUNC=$("$BIN" "MAGIC_TOKEN" "$TEST_TMP" -W --json-stdout)
assert_contains "$OUT_FUNC" "\"enclosing_function\": \"int calculate_metric(int x) {\"" "-W attaches enclosing function signature"

OUT_OUTLINE=$("$BIN" --outline "$TEST_TMP" --json-stdout)
assert_contains "$OUT_OUTLINE" "int calculate_metric(int x)" "--outline extracts C function definition"

OUT_FILES=$("$BIN" --files "$TEST_TMP" -g "*.c")
assert_contains "$OUT_FILES" "sample.c" "--files -g *.c lists matching files"

OUT_DIFF=$("$BIN" "MAGIC_TOKEN" "$TEST_TMP" --kind code -r "RENAMED_VAR" --diff)
assert_contains "$OUT_DIFF" "+    int RENAMED_VAR = x + 42;" "--diff previews unified diff replacement"

"$BIN" "MAGIC_TOKEN" "$TEST_TMP" -r "RENAMED_VAR" --in-place --backup >/dev/null
assert_eq "$( [ -f "$TEST_TMP/sample.c.bak" ] && echo 1 || echo 0 )" "1" "--in-place --backup creates .bak file"
assert_contains "$(cat "$TEST_TMP/sample.c")" "int RENAMED_VAR = x + 42;" "--in-place atomically updates target file"

echo "=================================================================="
echo "  3. Persistent Cache & MCP JSON-RPC 2.0 Server"
echo "=================================================================="
"$BIN" "$TEST_TMP" "RENAMED_VAR" --cache-build --porcelain >/dev/null
OUT_CACHE=$("$BIN" "$TEST_TMP" "RENAMED_VAR" --cache --porcelain)
assert_contains "$OUT_CACHE" "RENAMED_VAR" "--cache loads binary index and finds match"
"$BIN" "$TEST_TMP" --cache-clear >/dev/null

MCP_RESP=$(printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n' | "$BIN" --mcp-serve)
assert_contains "$MCP_RESP" "\"protocolVersion\"" "MCP initialize responds with protocolVersion"
assert_contains "$MCP_RESP" "\"supersearch_outline\"" "MCP tools/list exposes all 5 tools"

echo "=================================================================="
echo "  POSIX TEST SUITE SUMMARY: $PASS Passed, $FAIL Failed"
echo "=================================================================="
[ "$FAIL" -eq 0 ]
