#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "Starting Mitral Integration Test..."

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        echo "Cleaning up Mitral server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

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

docker exec mitral-redis redis-cli FLUSHALL > /dev/null

echo "Sending 7 rapid requests to localhost:8080..."
SUCCESS_COUNT=0
REJECT_COUNT=0

for i in {1..7}; do
    STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
    
    if [ "$STATUS" -eq 200 ]; then
        ((SUCCESS_COUNT++))
    elif [ "$STATUS" -eq 429 ]; then
        ((REJECT_COUNT++))
    else
        echo "Warning: Received unexpected status code $STATUS"
    fi
done

echo "Simulating 11-second expiration (Force clearing Redis key)..."
docker exec mitral-redis redis-cli FLUSHALL > /dev/null

echo "-----------------------------------"
RECOVERY_STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080)
if [ "$RECOVERY_STATUS" -eq 200 ]; then
    echo -e "${GREEN}[PASS] System successfully recovered after simulated expiration.${NC}"
else
    echo -e "${RED}[FAIL] System failed to recover. Expected 200, got $RECOVERY_STATUS.${NC}"
    exit 1
fi

if [ "$SUCCESS_COUNT" -eq 5 ] && [ "$REJECT_COUNT" -eq 2 ]; then
    echo -e "${GREEN}[PASS] Fixed Window Counter enforced exactly 5 limits and rejected 2.${NC}" # update to Token Bucket once implemented in phase 3
    exit 0
else
    echo -e "${RED}[FAIL] Expected 5 successes and 2 rejections.${NC}"
    echo "Got: $SUCCESS_COUNT successes, $REJECT_COUNT rejections."
    exit 1
fi