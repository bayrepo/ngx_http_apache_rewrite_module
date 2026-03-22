#!/bin/bash

# ============================================
# WordPress .htaccess Rules Test Script
# ============================================
# This script tests each rule from cms/wordpress/.htaccess
# Assumption: Site root is mapped to /home/alexey/projects/workspace-zed/test1/cms/wordpress
# Domain: test.my.brp
# ============================================

BASE_URL="http://test.my.brp"

echo "=============================================="
echo "WordPress .htaccess Rules Test Suite"
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
    local headers="$3"  # Optional: additional curl -H header flags (can be empty)
    local expected_status="$4"  # e.g., 403, 404, 200
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
echo "1. HTTP_AUTHORIZATION Header Rewrite Rule"
echo "=============================================="
# Test that Authorization header is properly handled by WordPress REST API mock
# This rule copies HTTP Authorization header to HTTP_AUTHORIZATION env var via mod_rewrite [E=...]
test_rule_content "WordPress REST API: Authorization header passed through mod_rewrite" \
    "$BASE_URL/wordpress/wp-json/wp/v2/posts" \
    "Authorization: secret_token_123" \
    "200" \
    "\"status\": \"success\""

# Additional test: verify token verification in response
test_rule_content "WordPress REST API: Token verified correctly" \
    "$BASE_URL/wordpress/wp-json/wp/v2/posts" \
    "" \
    "401" \
    "\"message\": \"unauth\""

echo ""
echo "=============================================="
echo "2. RewriteBase Configuration"
echo "=============================================="
# RewriteBase / sets base path to root - verifies routing works correctly
test_rule_content "Base path routing (root directory)" \
    "$BASE_URL/wordpress/" \
    "" \
    "200" \
    "WordPress Content Route"

echo ""
echo "=============================================="
echo "3. Direct index.php Access Protection"
echo "=============================================="
# RewriteRule ^index\.php$ - [L] - direct access to index.php should return 200 OK
test_rule_content "Direct index.php access (not rewritten)" \
    "$BASE_URL/wordpress/index.php" \
    "" \
    "200" \
    "WordPress Content Route"

echo ""
echo "=============================================="
echo "4. WordPress Core Routing Rules"
echo "=============================================="
# Test non-existing file routes through index.php (!-f condition)
test_rule_content "Non-existing file routing (routes to index.php)" \
    "$BASE_URL/wordpress/nonexistent-page/" \
    "" \
    "200" \
    "WordPress Content Route"

# Test existing file access - should return 200 OK (!-f passes for existing files)
test_rule "Existing file access (existing.jpg)" \
    "$BASE_URL/wordpress/existing.jpg" \
    "200"

# Test non-existing directory routing - should route to index.php (!-d condition)
test_rule_content "Non-existing directory routing (routes to index.php)" \
    "$BASE_URL/wordpress/nonexistent-dir/" \
    "" \
    "200" \
    "WordPress Content Route"

# Test existing directory access - should return 200 OK (!-d passes for existing dirs)
test_rule "Existing directory access (somedir/)" \
    "$BASE_URL/wordpress/somedir/" \
    "200"

echo ""
echo "=============================================="
echo "Test Suite Complete"
echo "=============================================="
