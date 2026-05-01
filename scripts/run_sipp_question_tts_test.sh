#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
SCRIPTS_DIR="$ROOT_DIR/scripts"
EMULATOR_DIR="$ROOT_DIR/src/emulator"
PYTHON_BIN="${PYTHON_BIN:-}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT_DIR/logs/sipp_question_test}"
QUESTION_TEXT="${QUESTION_TEXT:-안녕하세요. 오늘 영업시간이 어떻게 되나요?}"
QUESTION_WAV="${QUESTION_WAV:-$ARTIFACT_DIR/question.wav}"
QUESTION_PCAP="${QUESTION_PCAP:-$ARTIFACT_DIR/question.pcap}"
QUESTION_PCAP_MODE="${QUESTION_PCAP_MODE:-energy-map}"
SCENARIO_PCAP="$SCRIPTS_DIR/question_runtime.pcap"
TARGET_IP="${TARGET_IP:-127.0.0.1}"
TARGET_PORT="${TARGET_PORT:-5060}"
SERVICE_EXT="${SERVICE_EXT:-1004}"
SIPP_MEDIA_PORT="${SIPP_MEDIA_PORT:-40000}"
WAIT_MS="${WAIT_MS:-10000}"
AI_PORT="${AI_PORT:-55051}"

if [[ "$ARTIFACT_DIR" != /* ]]; then
  ARTIFACT_DIR="$ROOT_DIR/$ARTIFACT_DIR"
fi

if [[ -z "$PYTHON_BIN" && -x "$EMULATOR_DIR/venv/bin/python3" ]]; then
  PYTHON_BIN="$EMULATOR_DIR/venv/bin/python3"
fi

if [[ -z "$PYTHON_BIN" ]]; then
  PYTHON_BIN="$(command -v python3 || true)"
fi

if [[ -z "$PYTHON_BIN" || ! -x "$PYTHON_BIN" ]]; then
  echo "Error: python3 not found."
  exit 1
fi

mkdir -p "$ARTIFACT_DIR"

cleanup() {
  local exit_code=$?
  rm -f "$SCENARIO_PCAP"
  if [[ -n "${RTP_REPLAY_PID:-}" ]]; then kill "$RTP_REPLAY_PID" 2>/dev/null || true; fi
  if [[ -n "${SIPP_PID:-}" ]]; then kill "$SIPP_PID" 2>/dev/null || true; fi
  if [[ -n "${VBGW_PID:-}" ]]; then kill "$VBGW_PID" 2>/dev/null || true; fi
  if [[ -n "${MOCK_PID:-}" ]]; then kill "$MOCK_PID" 2>/dev/null || true; fi
  wait "${RTP_REPLAY_PID:-}" 2>/dev/null || true
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

if [[ ! -f "$EMULATOR_DIR/mock_server.py" ]]; then
  echo "Error: mock server not found."
  exit 1
fi

if [[ ! -f "$QUESTION_PCAP" ]]; then
  if [[ ! -f "$QUESTION_WAV" ]]; then
    if ! command -v say >/dev/null 2>&1; then
      echo "Error: QUESTION_PCAP not provided and say unavailable to synthesize WAV."
      exit 1
    fi
    say -o "$QUESTION_WAV" --file-format=WAVE --data-format=LEI16@16000 "$QUESTION_TEXT"
  fi

  "$PYTHON_BIN" - <<'PY' "$QUESTION_WAV"
import sys
import wave

with wave.open(sys.argv[1], "rb") as wf:
    if wf.getnframes() <= 0:
        raise SystemExit(f"Generated WAV has no audio frames: {sys.argv[1]}")
PY

  "$PYTHON_BIN" "$SCRIPTS_DIR/build_question_pcap.py" \
    --wav "$QUESTION_WAV" \
    --out "$QUESTION_PCAP" \
    --src-port "$SIPP_MEDIA_PORT" \
    --dst-port 16000 \
    --mode "$QUESTION_PCAP_MODE"
fi

cp "$QUESTION_PCAP" "$SCENARIO_PCAP"

echo "[1/4] Starting mock AI server..."
cd "$EMULATOR_DIR"
MOCK_SERVER_ADDR="127.0.0.1:${AI_PORT}" "$PYTHON_BIN" mock_server.py > "$ARTIFACT_DIR/mock_server.log" 2>&1 &
MOCK_PID=$!
cd "$ROOT_DIR"
sleep 2

echo "[2/4] Starting vbgw..."
export AI_ENGINE_ADDR="127.0.0.1:${AI_PORT}"
export GRPC_USE_TLS=0
export LOG_LEVEL="${LOG_LEVEL:-debug}"
export PJSIP_LOG_LEVEL="${PJSIP_LOG_LEVEL:-4}"
export PJSIP_NULL_AUDIO=1
export SIP_PORT="$TARGET_PORT"
export RTP_PORT_MIN="${RTP_PORT_MIN:-16000}"
export RTP_PORT_MAX="${RTP_PORT_MAX:-16000}"
export HTTP_PORT="${HTTP_PORT:-8080}"
"$BUILD_DIR/vbgw" > "$ARTIFACT_DIR/vbgw.log" 2>&1 &
VBGW_PID=$!
sleep 4

echo "[3/4] Running SIPp question -> TTS scenario..."
"$PYTHON_BIN" "$SCRIPTS_DIR/replay_rtp_pcap.py" \
  --pcap "$QUESTION_PCAP" \
  --target-ip 127.0.0.1 \
  --target-port "${RTP_PORT_MIN}" \
  --startup-delay 2.0 > "$ARTIFACT_DIR/rtp_replay.log" 2>&1 &
RTP_REPLAY_PID=$!

set +e
sipp "$TARGET_IP:$TARGET_PORT" \
  -sf "$SCRIPTS_DIR/sipp_question_tts.xml" \
  -m 1 \
  -s "$SERVICE_EXT" \
  -i 127.0.0.1 \
  -mi 127.0.0.1 \
  -mp "$SIPP_MEDIA_PORT" \
  -trace_msg \
  -trace_err \
  -trace_rtt \
  -timeout 20000 \
  -recv_timeout 20000 \
  -nd
SIPP_EXIT=$?
set -e

echo "[4/4] Collecting quick summary..."
echo "Artifacts: $ARTIFACT_DIR"
tail -n 20 "$ARTIFACT_DIR/mock_server.log" || true
tail -n 20 "$ARTIFACT_DIR/vbgw.log" || true

if [[ "$SIPP_EXIT" -ne 0 ]]; then
  echo "SIPp scenario failed with exit code $SIPP_EXIT"
  exit "$SIPP_EXIT"
fi

if ! rg -n "User started speaking|Sending mock STT and TTS response|Streaming generated audio chunks" "$ARTIFACT_DIR/mock_server.log" >/dev/null 2>&1; then
  echo "Expected STT/TTS flow markers were not found in mock_server.log"
  exit 2
fi

echo "SIPp question PCAP test completed successfully."
