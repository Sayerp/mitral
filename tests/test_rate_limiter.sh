#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "Starting Mitral Token Bucket Integration Test..."

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        echo "Cleaning up Mitral server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

docker exec mitral-redis redis-cli FLUSHALL > /dev/null

./mitral > /dev/null 2>&1 &
SERVER_PID=$!

TIMEOUT=50
while ! nc -z localhost 8080 >/dev/null 2>&1; do
    sleep 0.1
    ((TIMEOUT--))
    if [ "$TIMEOUT" -le 0 ]; then
        echo -e "${RED}[FAIL] Mitral server failed to start on port 8080.${NC}"
        exit 1
    fi
done

echo "-----------------------------------"
echo "Phase 1: Burst Capacity Test"
for i in {1..5}; do
    STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
    if [ "$STATUS" -ne 200 ]; then 
        echo -e "${RED}[FAIL] Request $i should have been 200 OK.${NC}"
        exit 1
    fi
done
echo -e "${GREEN}[PASS] 5 initial tokens successfully consumed.${NC}"

echo "-----------------------------------"
echo "Phase 2: Strict Limit Enforcement"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
if [ "$STATUS" -ne 429 ]; then 
    echo -e "${RED}[FAIL] 6th request should have been 429 Too Many Requests.${NC}"
    exit 1
fi
echo -e "${GREEN}[PASS] 6th request correctly blocked.${NC}"

echo "-----------------------------------"
echo "Phase 3: The Fractional Drip Recovery"
echo "Waiting ~1.1 seconds for tokens to drip..."

T_DEPLETED_NS=$(date +%s%N)
sleep 1.1
T_AFTER_SLEEP_NS=$(date +%s%N)

ELAPSED_NS=$((T_AFTER_SLEEP_NS - T_DEPLETED_NS))
EXPECTED_DRIP=$((ELAPSED_NS / 1000000000))
if [ "$EXPECTED_DRIP" -gt 5 ]; then
    EXPECTED_DRIP=5
fi
if [ "$EXPECTED_DRIP" -lt 1 ]; then
    echo -e "${RED}[FAIL] Only $((ELAPSED_NS / 1000000))ms elapsed -- not enough to test drip recovery.${NC}"
    exit 1
fi
echo "Measured elapsed: $((ELAPSED_NS / 1000000))ms -> expecting $EXPECTED_DRIP token(s) recovered"

for ((i = 1; i <= EXPECTED_DRIP; i++)); do
    STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
    if [ "$STATUS" -ne 200 ]; then
        echo -e "${RED}[FAIL] Recovered request $i failed (status $STATUS). Expected $EXPECTED_DRIP token(s) after measured elapsed time.${NC}"
        exit 1
    fi
done
echo -e "${GREEN}[PASS] $EXPECTED_DRIP token(s) successfully recovered, matching measured elapsed time.${NC}"

echo "-----------------------------------"
echo "Phase 4: The Token Math Check"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
if [ "$STATUS" -ne 429 ]; then
    echo -e "${RED}[FAIL] Request after draining recovered tokens succeeded. The bucket gave back too many tokens!${NC}"
    exit 1
fi
echo -e "${GREEN}[PASS] Fractional math verified. No extra tokens were granted.${NC}"

echo "-----------------------------------"
echo -e "${GREEN}[SUCCESS] Token Bucket architecture fully verified!${NC}"