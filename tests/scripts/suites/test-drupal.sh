#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8082"

suite_start "Drupal — port 8082"

# 1. Hidden files blocked (403)
test_status "Block .htaccess (403)" \
    "$BASE/.htaccess" "403"

test_status "Block .htpasswd (403)" \
    "$BASE/.htpasswd" "403"

# 2. .well-known/robots.txt — negative lookahead now works correctly
test_content "Allow .well-known/robots.txt (200)" \
    "$BASE/.well-known/robots.txt" "200" "User-agent:"

# 3. install.php / rebuild.php — redirect now works after server-level engine fix
test_redirect "install.php redirects to core/install.php (301)" \
    "$BASE/install.php" "301" "core/install.php"

test_redirect "rebuild.php redirects to core/rebuild.php (301)" \
    "$BASE/rebuild.php" "301" "core/rebuild.php"

# 4. favicon.ico — negative lookahead fix: no longer over-blocked
test_status "favicon.ico accessible (200)" \
    "$BASE/favicon.ico" "200"

# 5. Non-existing page routes to index.php
test_content "Non-existing page routes via index.php" \
    "$BASE/nonexistent-page/" "200" "Drupal Content Route"

# 6. Core test files excluded from blocking (200)
test_status "core/modules/system/tests/https.php accessible (200)" \
    "$BASE/core/modules/system/tests/https.php" "200"

test_status "core/modules/system/tests/http.php accessible (200)" \
    "$BASE/core/modules/system/tests/http.php" "200"

# 7. Authorization header passthrough — now works after lookahead fix
test_content "Authorization header passed to PHP" \
    "$BASE/rest/api/v1" "200" "Drupal Content Route" \
    "Authorization: Bearer token_abc123"

suite_end
