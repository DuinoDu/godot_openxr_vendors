#!/usr/bin/env bash
set -euo pipefail

# Installs the Android Build Template into this project using Godot CLI.
# Usage:
#   GODOT_BIN=/path/to/godot4 ./scripts/install_android_build_template.sh

PROJECT_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
cd "$PROJECT_ROOT_DIR"

GODOT_BIN="${GODOT_BIN:-}"
if [[ -z "${GODOT_BIN}" ]]; then
  if command -v godot4 >/dev/null 2>&1; then
    GODOT_BIN=godot4
  elif command -v godot >/dev/null 2>&1; then
    GODOT_BIN=godot
  else
    echo "[ERROR] Could not find Godot binary. Set GODOT_BIN to your Godot 4 executable." >&2
    exit 1
  fi
fi

echo "[INFO] Using GODOT_BIN=${GODOT_BIN}"

# # Redirect user/config dirs to project-local paths to avoid permission issues.
# export XDG_DATA_HOME="$PWD/.godot_user_data"
# export XDG_CONFIG_HOME="$PWD/.godot_user_config"
# mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME"

set -x
"${GODOT_BIN}" --headless --path . --install-android-build-template --quit
set +x

if [[ -f android/build.gradle ]]; then
  echo "[OK] Android build template installed at: android/"
else
  echo "[ERROR] Android build template installation did not create android/ directory." >&2
  exit 2
fi

