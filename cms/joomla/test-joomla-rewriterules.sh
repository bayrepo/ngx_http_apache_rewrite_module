#!/bin/bash

# ============================================
# Joomla .htaccess Rules Test Script
# ============================================
# This script tests each rule from cms/joomla/.htaccess
# Assumption: Site root is mapped to /home/alexey/projects/workspace-zed/test1/cms/joomla
# Domain: test.my.brp
# ============================================

BASE_URL="http://test.my.brp"

echo "=============================================="
echo "Joomla .htaccess Rules Test Suite"
echo "=============================================="
echo ""

# Function to test a rule and report result (status only)
test_rule() {
    local description="$1"
    local url="$2"
    local expected_status="$3"  # e.g., 403, 404, 200, 301

    echo "--- Test: $description ---"
    response=$(curl -s -o /dev/null -w "%{http_code}" "$url")

    if [ "$response" = "$expected_status" ]; then
        echo "✓ PASS (HTTP $response)"
    else
        echo "✗ FAIL (Expected: HTTP $expected_status, Got: HTTP $response)"
    fi
    echo ""
}

# Function to test a rule and verify content contains expected string
test_rule_content() {
    local description="$1"
    local url="$2"
    local headers="$3"  # Optional: additional curl -H header flags (can be empty)
    local expected_status="$4"  # e.g., 403, 404, 200, 301
    local expected_content="$5"  # Expected substring in response body

    echo "--- Test: $description ---"

    if [ -n "$headers" ]; then
        response=$(curl -s -H "$headers" "$url")
        http_code=$(curl -s -H "$headers" -o /dev/null -w "%{http_code}" "$url")
    else
        response=$(curl -s "$url")
        http_code=$(curl -s -o /dev/null -w "%{http_code}" "$url")
    fi

    # Check status code
    if [ "$http_code" != "$expected_status" ]; then
        echo "✗ FAIL (Status: HTTP $http_code, Expected: HTTP $expected_status)"
        return 1
    fi

    # Check content contains expected substring
    if [[ "$response" == *"$expected_content"* ]]; then
        echo "✓ PASS (HTTP $http_code, Content matches '$expected_content')"
    else
        echo "✗ FAIL (Content missing: '$expected_content') - Response:"
        echo "$response" | head -5
    fi
    echo ""
}

echo "=============================================="
echo "1. Base64 Encoded Payload Detection Rule"
echo "=============================================="
# Test base64_encode pattern detection in query string
test_rule "Base64 encoded payload blocked (base64_encode function call)" \
    "$BASE_URL/base64-test.php?data=base64_encode(secret_password)" \
    "403"

echo ""
echo "=============================================="
echo "2. Script Injection Pattern Detection Rule"
echo "============================================}"
# Test script injection pattern detection in query string (HTML entities)
test_rule "Script injection blocked (script tag encoded)" \
    "$BASE_URL/script-test.php?q=%3Cscript%3Ealert(1)%3E" \
    "403"

echo ""
echo "=============================================="
echo "3. GLOBALS Exploitation Detection Rule"
echo "============================================}"
# Test GLOBALS exploitation pattern detection in query string
test_rule "GLOBALS exploitation blocked (GLOBALS[secret] pattern)" \
    "$BASE_URL/globals-test.php?GLOBALS%5Buser%5D=admin" \
    "403"

echo ""
echo "=============================================="
echo "4. _REQUEST Manipulation Detection Rule"
echo "============================================}"
# Test _REQUEST manipulation pattern detection in query string
test_rule "_REQUEST manipulation blocked (_REQUEST[config] pattern)" \
    "$BASE_URL/request-test.php?_REQUEST%5Badmin%5D=true" \
    "403"

echo ""
echo "=============================================="
echo "5. HTTP Authorization Header Passing Rule"
echo "============================================}"
# Test that Authorization header is properly handled by Joomla REST API
test_rule_content "Joomla handles Authorization header (API request)" \
    "$BASE_URL/rest/api/v1" \
    "Authorization: Bearer joomla_token_abc" \
    "200" \
    "Joomla Content Route"

echo ""
echo "=============================================="
echo "6. Joomla API Routing Rule (/api/)"
echo "============================================}"
# Test /api/index.php routing - should route to api/index.php (!-f, !-d pass for non-existing files)
test_rule_content "API index.php routing (routes to /api/index.php)" \
    "$BASE_URL/api/" \
    "Authorization: secret_token_123" \
    "200" \
    "\"status\": \"success\""

# Additional test: verify token verification in response
test_rule_content "Direct /api/index.php file access" \
    "$BASE_URL/api/index.php" \
    "" \
    "401" \
    "\"message\": \"unauth\""

echo ""
echo "=============================================="
echo "7. Main Joomla Routing Rule (/index.php exclusion)"
echo "============================================}"
# Test that /api/index.php is NOT routed to main index.php (!^/api/index\.php passes)
test_rule_content "/api/index.php excluded from main routing (direct file access)" \
    "$BASE_URL/api/index.php" \
    "" \
    "200" \
    "Joomla API Configuration Loaded"

# Test non-existing file routing through main index.php (!-f AND !-d pass)
test_rule_content "Non-existing page routing (routes to /index.php)" \
    "$BASE_URL/nonexistent-page/" \
    "" \
    "200" \
    "Joomla Content Route"

# Test existing directory access (!-d condition passes) - should return 200 OK
test_rule "Existing directory access (somedir/)" \
    "$BASE_URL/somedir/" \
    "200"

echo ""
echo "=============================================="
echo "8. Joomla index.php reditercting"
echo "============================================}"
# Test that /api/index.php is NOT routed to main index.php (!^/api/index\.php passes)
test_rule_content "/index.php/component/config?view=modules accessing" \
    "$BASE_URL/index.php/component/config?view=modules" \
    "" \
    "200" \
    "Joomla Content Route"

echo ""
echo "=============================================="
echo "Test Suite Complete"
echo "=============================================="
