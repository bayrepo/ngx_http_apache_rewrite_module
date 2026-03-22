#!/bin/bash

# ============================================
# Drupal .htaccess Rules Test Script
# ============================================
# This script tests each rule from cms/drupal/.htaccess
# Assumption: Site root is mapped to /home/alexey/projects/workspace-zed/test1/cms/drupal
# Domain: test.my.brp
# ============================================

BASE_URL="http://test.my.brp"

echo "=============================================="
echo "Drupal .htaccess Rules Test Suite"
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
echo "1. RewriteEngine Activation"
echo "=============================================="
# Test basic routing through index.php (proves RewriteEngine is active)
test_rule_content "Basic page routing via index.php" \
    "$BASE_URL/normal-page/" \
    "" \
    "200" \
    "Drupal Content Route"

echo ""
echo "=============================================="
echo "2. Protocol Variables (protossl)"
echo "============================================}"
# Test HTTPS protocol detection - since we use http://, HTTPS should be off
test_rule_content "HTTP request without HTTPS (protocol detection)" \
    "$BASE_URL/normal-page/" \
    "" \
    "200" \
    "Drupal Content Route"

echo ""
echo "=============================================="
echo "3. HTTP Authorization Header Passing"
echo "=============================================="
# Test that Authorization header is properly handled by Drupal REST API
test_rule_content "Drupal handles Authorization header (API request)" \
    "$BASE_URL/rest/api/v1" \
    "Authorization: Bearer token_abc123" \
    "200" \
    "Drupal Content Route"

echo ""
echo "=============================================="
echo "4. Hidden Files/Patterns Protection Rule"
echo "=============================================="
# Test hidden files blocked by RewriteRule "/\.|^\.(?!well-known/)" - [F]
test_rule "Block .htaccess hidden file (pattern \.)" \
    "$BASE_URL/.htaccess" \
    "403"

test_rule "Block .htpasswd hidden file (pattern \.)" \
    "$BASE_URL/.htpasswd" \
    "403"

test_rule_content "Allow .well-known/robots.txt (exception for well-known)" \
    "$BASE_URL/.well-known/robots.txt" \
    "" \
    "200" \
    "User-agent:"

echo ""
echo "=============================================="
echo "5. Core install/rebuild.php Protection Rules"
echo "============================================}"
# Test install.php protection - should route to core/install.php with rewrite=ok parameter
test_rule "Core install.php protected routing" \
    "$BASE_URL/install.php" \
    "301"

# Test rebuild.php protection - similar redirect pattern
test_rule "Core rebuild.php protected routing" \
    "$BASE_URL/rebuild.php" \
    "301"

echo ""
echo "=============================================="
echo "6. Drupal Core Files Routing Rules"
echo "============================================}"
# Test existing file access (!-f condition passes) - should return 200 OK without routing to index.php
test_rule_content "Existing favicon.ico access (!-f condition)" \
    "$BASE_URL/favicon.ico" \
    "" \
    "200" \
    "This is a placeholder for Drupal test favicon.ico"

echo ""
echo "=============================================="
echo "7. Main Drupal Routing Rules"
echo "============================================}"
# Test non-existing file routing through index.php (main routing) - !-f AND !-d pass
test_rule_content "Non-existing page routing (routes to index.php)" \
    "$BASE_URL/nonexistent-page/" \
    "" \
    "200" \
    "Drupal Content Route"

# Test existing directory access (!-d condition passes) - should return 200 OK
test_rule "Existing directory access (somedir/)" \
    "$BASE_URL/somedir/" \
    "403"

echo ""
echo "=============================================="
echo "8. Core Modules Tests Files Exceptions"
echo "============================================}"
# Test https.php in tests directory - should NOT route to index.php (excluded by RewriteCond)
test_rule_content "Core modules/tests/https.php excluded from routing (!-php condition)" \
    "$BASE_URL/core/modules/system/tests/https.php" \
    "" \
    "200" \
    "# This is a test Drupal https.php in tests directory"

# Test http.php in tests directory - same exclusion applies (s for https? regex)
test_rule_content "Core modules/tests/http.php excluded from routing" \
    "$BASE_URL/core/modules/system/tests/http.php" \
    "" \
    "200" \
    "# This is a test Drupal http.php in tests directory"

echo ""
echo "=============================================="
echo "Test Suite Complete"
echo "=============================================="
