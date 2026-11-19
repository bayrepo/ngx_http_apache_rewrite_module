#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8083"

suite_start "WordPress — port 8083"

# WordPress REST API routing via wp_api.php
test_content "REST API: Authorization header passed through mod_rewrite" \
    "$BASE/wp-json/wp/v2/posts" "200" '"status": "success"' \
    "Authorization: secret_token_123"

test_content "REST API: no auth returns 401" \
    "$BASE/wp-json/wp/v2/posts" "401" '"message": "unauth"'

# 3. RewriteBase routing (root directory)
test_content "Base path routing via index.php" \
    "$BASE/" "200" "WordPress Content Route"

# 4. Direct index.php access (not rewritten)
test_content "Direct index.php access returns 200" \
    "$BASE/index.php" "200" "WordPress Content Route"

# 5. Non-existing file routes to index.php (!-f)
test_content "Non-existing file routes via index.php" \
    "$BASE/nonexistent-page/" "200" "WordPress Content Route"

# 6. Existing file access (200)
test_status "Existing existing.jpg returns 200" \
    "$BASE/existing.jpg" "200"

# 7. Non-existing directory routes to index.php (!-d)
test_content "Non-existing directory routes via index.php" \
    "$BASE/nonexistent-dir/" "200" "WordPress Content Route"

# 8. Existing directory access
# NOTE: somedir/ has .gitkeep inside, directory exists but autoindex is off
# and there is no index file, so nginx returns 403
test_status "Existing somedir/ without index returns 403" \
    "$BASE/somedir/" "403"

# 9. No duplicate headers when E=HTTP_AUTHORIZATION replaces existing header
# WordPress .htaccess has: RewriteRule .* - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
# When client sends Authorization header, module should REPLACE (not duplicate) it
test_content "No duplicate HTTP_AUTHORIZATION header (dedup check)" \
    "$BASE/header_check.php" "200" '"no_duplicates": true' \
    "Authorization: Bearer test_dedup_token"

suite_end
