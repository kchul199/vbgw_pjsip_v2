#!/bin/bash

# VBGW Functional Test Runner (Hybrid Mode)
# SIPp handles SIP signalling
# Python handles RTP transmission (bypassing macOS raw socket restrictions)

PROJECT_ROOT=$(pwd)
BUILD_DIR="${PROJECT_ROOT}/build"
SCRIPTS_DIR="${PROJECT_ROOT}/scripts"
EMULATOR_DIR="${PROJECT_ROOT}/src/emulator"

# 1. Cleanup
echo "🧹 Cleaning up existing processes..."
killall vbgw 2>/dev/null
killall python3 2>/dev/null
sleep 2

# 2. Start AI Mock Server
echo "🚀 Starting AI Mock Server..."
cd "${EMULATOR_DIR}"
python3 mock_server.py > mock_server.log 2>&1 &
MOCK_PID=$!
cd "${PROJECT_ROOT}"

# 3. Start VBGW with fixed RTP port for testing
echo "🚀 Starting VBGW..."
export AI_ENGINE_ADDR="127.0.0.1:50051"
export LOG_LEVEL="debug"
export RTP_PORT_MIN=16000
export RTP_PORT_MAX=16000
"${BUILD_DIR}/vbgw" > vbgw_functional.log 2>&1 &
VBGW_PID=$!

sleep 3

# 4. Start Python RTP Sender in background (waits 2s then sends)
echo "📦 Starting RTP Sender (The Question)..."
python3 "${SCRIPTS_DIR}/send_rtp_question.py" > rtp_sender.log 2>&1 &
RTP_PID=$!

# 5. Run SIPp
echo "📞 Running SIPp (The Caller)..."
sipp 127.0.0.1:5060 -sf "${SCRIPTS_DIR}/functional_test.xml" -m 1 -s 1004 -trace_msg -trace_err
SIPP_EXIT=$?

# 6. Final check
echo "🛑 Shutting down..."
kill $VBGW_PID 2>/dev/null
kill $MOCK_PID 2>/dev/null

if [ $SIPP_EXIT -eq 0 ]; then
    echo "✅ Functional Test PASSED (Signalling)!"
    echo "Check mock_server.log to verify STT/TTS flow."
else
    echo "❌ Functional Test FAILED! (Exit code: $SIPP_EXIT)"
fi

exit $SIPP_EXIT
