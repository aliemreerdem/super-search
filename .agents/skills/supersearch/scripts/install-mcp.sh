#!/usr/bin/env bash
# ==============================================================================
# SuperSearch — Linux & macOS Skill Installer & MCP Configuration Helper
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$SKILL_ROOT/../../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"
SUPER_BIN="$BIN_DIR/supersearch"

ADD_TO_PATH=0
SKIP_GLOBAL_SKILL=0

for arg in "$@"; do
    case "$arg" in
        --add-to-path|-AddToPath) ADD_TO_PATH=1 ;;
        --skip-global-skill|-SkipGlobalSkill) SKIP_GLOBAL_SKILL=1 ;;
    esac
done

if [ ! -x "$SUPER_BIN" ]; then
    echo "[INFO] supersearch binary not found. Building from source via make..."
    (cd "$PROJECT_ROOT" && make)
fi

if [ "$ADD_TO_PATH" -eq 1 ]; then
    SHELL_RC="$HOME/.bashrc"
    if [ -n "${ZSH_VERSION:-}" ] || [ "$(basename "${SHELL:-}")" = "zsh" ]; then
        SHELL_RC="$HOME/.zshrc"
    fi
    if ! grep -Fq "$BIN_DIR" "$SHELL_RC" 2>/dev/null; then
        echo "export PATH=\"\$PATH:$BIN_DIR\"" >> "$SHELL_RC"
        echo "[OK] Added '$BIN_DIR' to $SHELL_RC"
    else
        echo "[OK] '$BIN_DIR' is already present in $SHELL_RC"
    fi
fi

if [ "$SKIP_GLOBAL_SKILL" -eq 0 ]; then
    GLOBAL_SKILLS_DIR="$HOME/.gemini/config/skills/supersearch"
    mkdir -p "$GLOBAL_SKILLS_DIR"
    cp -R "$SKILL_ROOT/"* "$GLOBAL_SKILLS_DIR/"
    echo "[OK] Installed SuperSearch skill globally to: $GLOBAL_SKILLS_DIR"
    echo "     Binary Path: $SUPER_BIN"
fi

echo ""
echo "[INFO] MCP Server Configuration (Claude Code / Cursor / Windsurf / Antigravity):"
"$SUPER_BIN" --mcp-config
