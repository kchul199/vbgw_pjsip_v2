#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
EMULATOR_DIR="$ROOT_DIR/src/emulator"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT_DIR/logs/load_test_10min}"

TARGET_IP="${TARGET_IP:-127.0.0.1}"
TARGET_PORT="${TARGET_PORT:-5060}"
SERVICE_EXT="${SERVICE_EXT:-1004}"
AI_PORT="${AI_PORT:-55051}"

CALL_RATE="${CALL_RATE:-10}"
TEST_DURATION_SEC="${TEST_DURATION_SEC:-600}"
TOTAL_CALLS="${TOTAL_CALLS:-$((CALL_RATE * TEST_DURATION_SEC))}"
HOLD_MS="${HOLD_MS:-10000}"
CONCURRENCY_LIMIT="${CONCURRENCY_LIMIT:-200}"
MAX_CONCURRENT_CALLS="${MAX_CONCURRENT_CALLS:-1000}"

PYTHON_BIN="${PYTHON_BIN:-}"
if [[ -z "$PYTHON_BIN" && -x "$EMULATOR_DIR/venv/bin/python3" ]]; then
  PYTHON_BIN="$EMULATOR_DIR/venv/bin/python3"
fi
if [[ -z "$PYTHON_BIN" ]]; then
  PYTHON_BIN="$(command -v python3 || true)"
fi

if [[ "$ARTIFACT_DIR" != /* ]]; then
  ARTIFACT_DIR="$ROOT_DIR/$ARTIFACT_DIR"
fi

mkdir -p "$ARTIFACT_DIR"

cleanup() {
  local exit_code=$?
  if [[ -n "${MONITOR_PID:-}" ]]; then kill "$MONITOR_PID" 2>/dev/null || true; fi
  if [[ -n "${SIPP_PID:-}" ]]; then kill "$SIPP_PID" 2>/dev/null || true; fi
  if [[ -n "${VBGW_PID:-}" ]]; then kill "$VBGW_PID" 2>/dev/null || true; fi
  if [[ -n "${MOCK_PID:-}" ]]; then kill "$MOCK_PID" 2>/dev/null || true; fi
  wait "${MONITOR_PID:-}" 2>/dev/null || true
  wait "${SIPP_PID:-}" 2>/dev/null || true
  wait "${VBGW_PID:-}" 2>/dev/null || true
  wait "${MOCK_PID:-}" 2>/dev/null || true
  exit "$exit_code"
}
trap cleanup EXIT

if ! command -v sipp >/dev/null 2>&1; then
  echo "Error: sipp is not installed."
  exit 1
fi

if [[ ! -x "$BUILD_DIR/vbgw" ]]; then
  echo "Error: $BUILD_DIR/vbgw not found."
  exit 1
fi

if [[ -z "$PYTHON_BIN" || ! -x "$PYTHON_BIN" ]]; then
  echo "Error: python3 not found."
  exit 1
fi

monitor_runtime() {
  while true; do
    {
      printf '[%s]\n' "$(date '+%Y-%m-%d %H:%M:%S')"
      if command -v curl >/dev/null 2>&1; then
        curl -s "http://127.0.0.1:${HTTP_PORT:-8080}/health" || true
        echo
      fi
      if command -v curl >/dev/null 2>&1; then
        curl -s "http://127.0.0.1:${HTTP_PORT:-8080}/metrics" | rg 'vbgw_(active_calls|grpc_active_sessions|grpc_stream_errors_total|grpc_dropped_frames_total)' || true
      fi
      echo
    } >> "$ARTIFACT_DIR/runtime_monitor.log"
    sleep 5
  done
}

echo "[1/4] Starting mock AI server..."
cd "$EMULATOR_DIR"
MOCK_SERVER_ADDR="127.0.0.1:${AI_PORT}" "$PYTHON_BIN" mock_server.py > "$ARTIFACT_DIR/mock_server_load.log" 2>&1 &
MOCK_PID=$!
cd "$ROOT_DIR"
sleep 2

echo "[2/4] Starting vbgw..."
export AI_ENGINE_ADDR="127.0.0.1:${AI_PORT}"
export GRPC_USE_TLS=0
export LOG_LEVEL="${LOG_LEVEL:-info}"
export PJSIP_LOG_LEVEL="${PJSIP_LOG_LEVEL:-3}"
export PJSIP_NULL_AUDIO=1
export SIP_PORT="$TARGET_PORT"
export HTTP_PORT="${HTTP_PORT:-8080}"
export MAX_CONCURRENT_CALLS
"$BUILD_DIR/vbgw" > "$ARTIFACT_DIR/vbgw_load.log" 2>&1 &
VBGW_PID=$!
sleep 5

echo "[3/4] Starting runtime monitor..."
monitor_runtime &
MONITOR_PID=$!

echo "[4/4] Running SIPp load test..."
set +e
sipp "$TARGET_IP:$TARGET_PORT" \
  -sn uac \
  -r "$CALL_RATE" \
  -m "$TOTAL_CALLS" \
  -l "$CONCURRENCY_LIMIT" \
  -d "$HOLD_MS" \
  -s "$SERVICE_EXT" \
  -trace_stat \
  -stf "$ARTIFACT_DIR/load_test_stats.csv" \
  -timeout 30000 \
  -recv_timeout 30000 \
  -nd
SIPP_EXIT=$?
set -e

echo "Artifacts: $ARTIFACT_DIR"
tail -n 20 "$ARTIFACT_DIR/mock_server_load.log" || true
tail -n 20 "$ARTIFACT_DIR/vbgw_load.log" || true

if [[ "$SIPP_EXIT" -ne 0 ]]; then
  echo "SIPp load test failed with exit code $SIPP_EXIT"
  exit "$SIPP_EXIT"
fi

echo "SIPp load test completed successfully."
