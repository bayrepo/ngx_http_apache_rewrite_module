#!/bin/bash
# ============================================================
# Test helpers for mod_rewrite integration tests
# ============================================================

TOTAL_PASS=0
TOTAL_FAIL=0
SUITE_PASS=0
SUITE_FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

suite_start() {
    local name="$1"
    SUITE_PASS=0
    SUITE_FAIL=0
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  $name${NC}"
    echo -e "${CYAN}============================================================${NC}"
}

RESULTS_FILE="${RESULTS_FILE:-/tmp/test-results.txt}"

suite_end() {
    echo ""
    echo -e "${CYAN}--- Suite Summary ---${NC}"
    echo -e "  Pass: ${GREEN}${SUITE_PASS}${NC}  Fail: ${RED}${SUITE_FAIL}${NC}"

    local prev_pass=0 prev_fail=0
    if [ -f "$RESULTS_FILE" ]; then
        read prev_pass prev_fail < "$RESULTS_FILE" 2>/dev/null || true
    fi
    echo "$((prev_pass + SUITE_PASS)) $((prev_fail + SUITE_FAIL))" > "$RESULTS_FILE"
}

test_status() {
    local description="$1"
    local url="$2"
    local expected="$3"

    local got
    got=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$url" 2>/dev/null)

    if [ "$got" = "$expected" ]; then
        echo -e "  ${GREEN}✓ PASS${NC} [$description] HTTP $got"
        SUITE_PASS=$((SUITE_PASS + 1))
    else
        echo -e "  ${RED}✗ FAIL${NC} [$description] Expected: $expected, Got: $got"
        SUITE_FAIL=$((SUITE_FAIL + 1))
    fi
}

test_content() {
    local description="$1"
    local url="$2"
    local expected_code="$3"
    local expected_substring="$4"
    local extra_headers="$5"

    local body http_code
    if [ -n "$extra_headers" ]; then
        body=$(curl -s -H "$extra_headers" --max-time 5 "$url" 2>/dev/null)
        http_code=$(curl -s -H "$extra_headers" -o /dev/null -w "%{http_code}" --max-time 5 "$url" 2>/dev/null)
    else
        body=$(curl -s --max-time 5 "$url" 2>/dev/null)
        http_code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$url" 2>/dev/null)
    fi

    if [ "$http_code" != "$expected_code" ]; then
        echo -e "  ${RED}✗ FAIL${NC} [$description] Status: expected $expected_code, got $http_code"
        SUITE_FAIL=$((SUITE_FAIL + 1))
        return
    fi

    if [[ "$body" == *"$expected_substring"* ]]; then
        echo -e "  ${GREEN}✓ PASS${NC} [$description] HTTP $http_code, content matched"
        SUITE_PASS=$((SUITE_PASS + 1))
    else
        echo -e "  ${RED}✗ FAIL${NC} [$description] HTTP $http_code, content missing: '$expected_substring'"
        SUITE_FAIL=$((SUITE_FAIL + 1))
    fi
}

test_redirect() {
    local description="$1"
    local url="$2"
    local expected_code="$3"
    local expected_location="$4"

    local http_code location
    http_code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 "$url" 2>/dev/null)
    location=$(curl -s -I --max-time 5 "$url" 2>/dev/null | grep -i "^Location:" | sed 's/Location: //i' | tr -d '\r')

    if [ "$http_code" != "$expected_code" ]; then
        echo -e "  ${RED}✗ FAIL${NC} [$description] Status: expected $expected_code, got $http_code"
        SUITE_FAIL=$((SUITE_FAIL + 1))
        return
    fi

    if [[ "$location" == *"$expected_location"* ]]; then
        echo -e "  ${GREEN}✓ PASS${NC} [$description] HTTP $http_code → $location"
        SUITE_PASS=$((SUITE_PASS + 1))
    else
        echo -e "  ${RED}✗ FAIL${NC} [$description] HTTP $http_code, Location: expected '*$expected_location*', got '$location'"
        SUITE_FAIL=$((SUITE_FAIL + 1))
    fi
}

print_total_summary() {
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  TOTAL RESULTS${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo -e "  Total Pass: ${GREEN}${TOTAL_PASS}${NC}"
    echo -e "  Total Fail: ${RED}${TOTAL_FAIL}${NC}"
    echo -e "${CYAN}============================================================${NC}"

    if [ "$TOTAL_FAIL" -gt 0 ]; then
        echo -e "${RED}SOME TESTS FAILED${NC}"
        return 1
    else
        echo -e "${GREEN}ALL TESTS PASSED${NC}"
        return 0
    fi
}
