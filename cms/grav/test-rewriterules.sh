#!/bin/bash

# ============================================
# Grav CMS .htaccess Rules Test Script
# ============================================
# This script tests each rule from cms/grav/.htaccess
# Domain: test.my.brp
# ============================================

BASE_URL="http://test.my.brp"

echo "=============================================="
echo "Grav CMS .htaccess Rules Test Suite"
echo "=============================================="
echo ""

# Function to test a rule and report result (status only)
test_rule() {
    local description="$1"
    local url="$2"
    local expected_status="$3"  # e.g., 403, 404, 200

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
    local expected_status="$3"  # e.g., 403, 404, 200
    local expected_content="$4"  # Expected substring in response body

    echo "--- Test: $description ---"
    response=$(curl -s "$url")
    http_code=$(curl -s -o /dev/null -w "%{http_code}" "$url")

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
echo "1. Security Rules - Test malicious patterns"
echo "=============================================="
# Template injection via query string with {{ }} Mustache syntax
test_rule "Template injection via query string ({{ }}" \
    "$BASE_URL/test-mustache.php?\{\{config.secret\}\}" \
    "403"

# Base64 encoded payloads (base64_encode function call)
test_rule "Base64 payload pattern" \
    "$BASE_URL/test.php?data=base64_encode(some_secret)" \
    "403"

# Script injection (<script> or HTML entities %3C)
test_rule "Script injection (encoded)" \
    "$BASE_URL/search.html?q=%3Cscript%25alert(1)%3E" \
    "403"

# GLOBALS exploitation attempt
test_rule "GLOBALS manipulation" \
    "$BASE_URL/leak.php?GLOBALS\[secret\]=exploit" \
    "403"

# _REQUEST manipulation (PHP superglobal abuse)
test_rule "_REQUEST manipulation" \
    "$BASE_URL/attack.php?_REQUEST\[user\]=admin" \
    "403"

echo ""
echo "=============================================="
echo "2. Grav CMS Core Routing Rules"
echo "=============================================="
# Test normal page routing through index.php (non-existing file -> index.php)
test_rule_content "Normal page routing via index.php" \
    "$BASE_URL/normal-page/" \
    "200" \
    "Grav CMS Content Route"

# Test existing file access - check status + content type/image
test_rule "Existing file access (existing.jpg)" \
    "$BASE_URL/existing.jpg" \
    "200"

# Test existing directory access
test_rule "Existing directory access (somedir/)" \
    "$BASE_URL/somedir/" \
    "200"

echo ""
echo "=============================================="
echo "3. Sensitive Directory Protection"
echo "=============================================="
# Block system, cache, logs, backups folders
test_rule "Block .git folder" "$BASE_URL/.git/secret.txt" "403"
test_rule "Block cache folder" "$BASE_URL/cache/test.txt" "403"
test_rule "Block bin folder" "$BASE_URL/bin/helper.php" "403"
test_rule "Block logs folder" "$BASE_URL/logs/app.log" "403"
test_rule "Block backup folder" "$BASE_URL/backup/archive.zip" "403"

# Block system and vendor directories with sensitive extensions
test_rule "Block system config.xml" "$BASE_URL/system/config.xml" "403"
test_rule "Block vendor module.txt" "$BASE_URL/vendor/module.txt" "403"

echo ""
echo "=============================================="
echo "4. File Extension Protection"
echo "=============================================="
# Block raw .md file access (content source files)
test_rule "Block raw .md file" "$BASE_URL/test.md" "403"

echo ""
echo "=============================================="
echo "5. Hidden Files/Directories Protection"
echo "=============================================="
test_rule "Block hidden .htpasswd" "$BASE_URL/.htpasswd" "403"
test_rule "Allow .well-known/robots.txt" "$BASE_URL/.well-known/robots.txt" "200"

echo ""
echo "=============================================="
echo "6. Critical Configuration File Protection"
echo "=============================================="
test_rule "Block LICENSE.txt" "$BASE_URL/LICENSE.txt" "403"
test_rule "Block composer.lock" "$BASE_URL/composer.lock" "403"
test_rule "Block composer.json" "$BASE_URL/composer.json" "403"
test_rule "Block .htaccess" "$BASE_URL/.htaccess" "403"

echo ""
echo "=============================================="
echo "Test Suite Complete"
echo "=============================================="
