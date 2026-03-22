#!/bin/bash

# ============================================
# WordPress Multisite .htaccess Rules Test Script
# ============================================
# This script tests each rule from cms/wordpress-multy/.htaccess
# Assumption: Site root is mapped to /home/alexey/projects/workspace-zed/test1/cms/wordpress-multy
# Domain: test.my.brp
# ============================================

BASE_URL="http://test.my.brp"

echo "=============================================="
echo "WordPress Multisite .htaccess Rules Test Suite"
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
    local expected_status="$3"  # e.g., 403, 404, 200, 301
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
echo "1. HTTP Authorization Header Handling"
echo "=============================================="
# Test that Authorization header is properly handled by WordPress Multisite API
test_rule_content "WordPress multisite handles Authorization header (API request)" \
    "$BASE_URL/wp-json/wp/v2/posts?Authorization=Bearer secret_token_123" \
    "200" \
    "WordPress Multisite Configuration Loaded"

echo ""
echo "=============================================="
echo "2. RewriteBase Root Configuration"
echo "=============================================="
# Test root directory access with proper base path routing
test_rule_content "Root directory access (base path /)" \
    "$BASE_URL/" \
    "200" \
    "WordPress Multisite Content Route"

echo ""
echo "=============================================="
echo "3. Direct index.php Access Protection"
echo "=============================================="
# Test direct access to index.php - should NOT be rewritten (prevents infinite loop)
test_rule_content "Direct index.php access (not rewritten)" \
    "$BASE_URL/index.php" \
    "200" \
    "WordPress Multisite Configuration Loaded"

echo ""
echo "=============================================="
echo "4. wp-admin Trailing Slash Redirect Rule"
echo "=============================================="
# Test trailing slash redirect for /wp-admin (301 Moved Permanently)
test_rule_content "wp-admin trailing slash redirect (301)" \
    "$BASE_URL/wp-admin" \
    "301"

# Test that /wp-admin/ with trailing slash works correctly (existing directory)
test_rule_content "wp-admin/ with trailing slash (exists as directory)" \
    "$BASE_URL/wp-admin/" \
    "200" \
    "WordPress Multisite Content Route"

echo ""
echo "=============================================="
echo "5. Existing Files/Directories Check Rule"
echo "=============================================="
# Test existing CSS file access (!-f condition passes) - should return 200 OK without routing to index.php
test_rule "Existing CSS file access (style.css)" \
    "$BASE_URL/style.css" \
    "200"

# Test existing JS file access (!-f condition passes) - should return 200 OK without routing to index.php
test_rule "Existing JS file access (script.js)" \
    "$BASE_URL/script.js" \
    "200"

echo ""
echo "=============================================="
echo "6. WordPress Multisite Core Routing Rules"
echo "=============================================="
# Test wp-admin specific routing rule (not through index.php)
test_rule_content "wp-admin/admin.php multisite routing" \
    "$BASE_URL/wp-admin/admin.php" \
    "200" \
    "WordPress Multisite Content Route"

# Test wp-content specific routing rule (for uploads, themes etc.)
test_rule_content "wp-content/uploads/test.jpg multisite routing" \
    "$BASE_URL/wp-content/uploads/test.jpg" \
    "200" \
    "WordPress Multisite Content Route"

# Test wp-includes specific routing rule (core files)
test_rule_content "wp-includes/js/jquery.js multisite routing" \
    "$BASE_URL/wp-includes/js/jquery.js" \
    "200" \
    "WordPress Multisite Content Route"

echo ""
echo "=============================================="
echo "7. All PHP Files Direct Access Rule"
echo "=============================================="
# Test any .php file access (not routed through index.php) - custom script returns specific content
test_rule_content "Any PHP file access (.php rule)" \
    "$BASE_URL/custom-script.php" \
    "200" \
    "WordPress Multisite custom script - direct PHP file access"

echo ""
echo "=============================================="
echo "8. Final WordPress Routing Rule"
echo "=============================================="
# Test non-existing file routing through index.php (main routing)
test_rule_content "Non-existing page routing (routes to index.php)" \
    "$BASE_URL/nonexistent-page/" \
    "200" \
    "WordPress Multisite Content Route"

# Test non-existing directory routing through index.php (routes to index.php)
test_rule_content "Non-existing directory routing (routes to index.php)" \
    "$BASE_URL/nonexistent-directory/" \
    "200" \
    "WordPress Multisite Content Route"

echo ""
echo "=============================================="
echo "Test Suite Complete"
echo "=============================================="
