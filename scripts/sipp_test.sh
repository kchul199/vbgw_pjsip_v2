#!/bin/bash
# SIPp Automated Load/E2E Test Script for VBGW

TARGET_IP=${1:-"127.0.0.1"}
TARGET_PORT=${2:-"5060"}
CALL_RATE=${3:-"10"}
MAX_CALLS=${4:-"100"}

if ! command -v sipp &> /dev/null; then
    echo "Error: SIPp is not installed. Please install it (e.g., apt install sip-tester)."
    exit 1
fi

echo "Starting SIPp test against VBGW at $TARGET_IP:$TARGET_PORT"
echo "Rate: $CALL_RATE calls/sec, Max Calls: $MAX_CALLS"

# Basic UAC scenario using SIPp's built-in scenario (or a custom XML if provided)
# -sn uac: standard UAC scenario
# -r: call rate
# -m: total maximum calls
# -s: dialing number

sipp -sn uac "$TARGET_IP:$TARGET_PORT" -r "$CALL_RATE" -m "$MAX_CALLS" -s 1004

if [ $? -eq 0 ]; then
    echo "SIPp test completed successfully."
else
    echo "SIPp test encountered errors."
fi
