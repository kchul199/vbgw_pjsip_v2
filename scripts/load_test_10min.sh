#!/bin/bash

# VBGW 10-Minute Load Test Script
# Duration: 10 minutes (600 seconds)
# Rate: 10 calls per second
# Total Calls: 6000
# Target: Local VBGW (127.0.0.1:5060)

PROJECT_ROOT=$(pwd)
BUILD_DIR="${PROJECT_ROOT}/build"
SCRIPTS_DIR="${PROJECT_ROOT}/scripts"
EMULATOR_DIR="${PROJECT_ROOT}/src/emulator"

# 1. Cleanup
echo "🧹 Cleaning up existing processes..."
killall vbgw 2>/dev/null
killall python3 2>/dev/null
sleep 2

# 2. Start AI Mock Server (with 200 workers)
echo "🚀 Starting AI Mock Server..."
cd "${EMULATOR_DIR}"
python3 mock_server.py > mock_server_load.log 2>&1 &
MOCK_PID=$!
cd "${PROJECT_ROOT}"

# 3. Start VBGW
echo "🚀 Starting VBGW..."
export AI_ENGINE_ADDR="127.0.0.1:50051"
export LOG_LEVEL="info"  # Reduce logging for performance
export MAX_CONCURRENT_CALLS=1000
"${BUILD_DIR}/vbgw" > vbgw_load.log 2>&1 &
VBGW_PID=$!

sleep 5 # Wait for initialization

# 4. Run SIPp Load Test
# -r 10: 10 calls per second
# -m 6000: total 6000 calls (10 minutes at 10 cps)
# -l 500: limit concurrent calls to 500
echo "🔥 Starting 10-minute load test (10 CPS, Total 6000 calls)..."
sipp 127.0.0.1:5060 \
    -sn uac \
    -r 10 \
    -m 6000 \
    -l 500 \
    -s 1004 \
    -trace_stat \
    -stf load_test_stats.csv

SIPP_EXIT=$?

# 5. Summary and Cleanup
echo "🛑 Load test finished."
kill $VBGW_PID 2>/dev/null
kill $MOCK_PID 2>/dev/null

if [ $SIPP_EXIT -eq 0 ]; then
    echo "✅ 10-Minute Load Test Completed Successfully!"
else
    echo "⚠️ Load test interrupted or failed (Exit code: $SIPP_EXIT)"
fi

echo "Detailed stats saved to load_test_stats.csv"
echo "System logs available in vbgw_load.log and mock_server_load.log"
