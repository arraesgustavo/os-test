#!/usr/bin/env bash
set -euo pipefail

DEFAULT_BIN="build/os_test_android"
REMOTE_PATH="/data/local/tmp/os_test"
PUSHED=0

cleanup() {
    if [[ "$PUSHED" -eq 1 ]]; then
        adb shell rm -f "$REMOTE_PATH" >/dev/null 2>&1 || true
    fi
}

trap 'exit 130' INT
trap 'exit 143' TERM
trap cleanup EXIT

if ! command -v adb >/dev/null 2>&1; then
    echo "Error: 'adb' not found in PATH. Install Android platform-tools first." >&2
    exit 1
fi

BIN_PATH="$DEFAULT_BIN"
if [[ $# -gt 0 && "$1" != -* ]]; then
    BIN_PATH="$1"
    shift
fi
EXTRA_ARGS=("$@")

if [[ ! -f "$BIN_PATH" ]]; then
    echo "Error: binary not found: '$BIN_PATH'" >&2
    echo "Build it first with 'make android' or 'make android-static'." >&2
    exit 1
fi

DEVICE_COUNT="$(adb devices | tr -d '\r' | awk 'NR > 1 && $2 == "device" { c++ } END { print c + 0 }')"
if [[ "$DEVICE_COUNT" -lt 1 ]]; then
    echo "Error: no Android device/emulator connected in 'device' state." >&2
    echo "Run 'adb devices' to check USB debugging or emulator status." >&2
    exit 1
fi

echo "[os-test] Pushing '$BIN_PATH' -> '$REMOTE_PATH'..."
PUSHED=1
adb push "$BIN_PATH" "$REMOTE_PATH" >/dev/null

adb shell chmod +x "$REMOTE_PATH"

echo "[os-test] Running on Android device..."
set +e
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    adb shell "$REMOTE_PATH" "${EXTRA_ARGS[@]}"
else
    adb shell "$REMOTE_PATH"
fi
RC=$?
set -e

exit "$RC"
