#!/bin/bash

echo "Starting php-fpm..."
php-fpm -D

echo "Starting nginx..."
nginx

echo "Waiting for services to be ready..."
for i in $(seq 1 30); do
    if curl -s -o /dev/null http://localhost:8081/ 2>/dev/null; then
        echo "Services are ready."
        break
    fi
    sleep 1
done

if ! curl -s -o /dev/null http://localhost:8081/ 2>/dev/null; then
    echo "ERROR: Services failed to start within 30 seconds"
    cat /var/log/nginx/error.log
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_FILE="/tmp/test-results.txt"
echo "0 0" > "$RESULTS_FILE"
export RESULTS_FILE

for suite in "$SCRIPT_DIR"/suites/test-*.sh; do
    if [ -f "$suite" ]; then
        bash "$suite"
    fi
done

read TOTAL_PASS TOTAL_FAIL < "$RESULTS_FILE"

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo -e "${CYAN}============================================================${NC}"
echo -e "${CYAN}  TOTAL RESULTS${NC}"
echo -e "${CYAN}============================================================${NC}"
echo -e "  Total Pass: ${GREEN}${TOTAL_PASS}${NC}"
echo -e "  Total Fail: ${RED}${TOTAL_FAIL}${NC}"
echo -e "${CYAN}============================================================${NC}"

if [ "$TOTAL_FAIL" -gt 0 ]; then
    echo -e "${RED}SOME TESTS FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
fi
