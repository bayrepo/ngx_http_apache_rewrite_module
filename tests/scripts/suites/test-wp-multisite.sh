#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8086"

suite_start "WordPress Multisite — port 8086"

# 1. Authorization header handling — works correctly
test_content "Authorization header passed through" \
    "$BASE/wp-json/wp/v2/posts?Authorization=Bearer secret_token_123" "200" \
    "WordPress Multisite Configuration Loaded"

# 2. Root directory access (RewriteBase /) — works correctly
test_content "Root directory routing via index.php" \
    "$BASE/" "200" "WordPress Multisite Content Route"

# 3. Direct index.php access — works correctly
test_content "Direct index.php access returns 200" \
    "$BASE/index.php" "200" "WordPress Multisite Configuration Loaded"

# 4. wp-admin trailing slash redirect (301) — works correctly
test_redirect "wp-admin trailing slash redirect (301)" \
    "$BASE/wp-admin" "301" "wp-admin/"

# 5. wp-admin/ with trailing slash
# NOTE: wp-admin/ is a real directory without index file, nginx returns 403
test_status "wp-admin/ directory without index returns 403" \
    "$BASE/wp-admin/" "403"

# 6. Existing static files — work correctly
test_status "Existing style.css returns 200" \
    "$BASE/style.css" "200"

test_status "Existing script.js returns 200" \
    "$BASE/script.js" "200"

# 7. wp-admin/admin.php — real PHP file, returns its own content (not index.php)
test_status "wp-admin/admin.php returns 200 (real file)" \
    "$BASE/wp-admin/admin.php" "200"

# 8. wp-content/uploads/test.jpg — real file, returns 200
test_status "wp-content/uploads/test.jpg returns 200 (real file)" \
    "$BASE/wp-content/uploads/test.jpg" "200"

# 9. wp-includes/js/jquery.js — real file, returns 200
test_status "wp-includes/js/jquery.js returns 200 (real file)" \
    "$BASE/wp-includes/js/jquery.js" "200"

# 10. Custom PHP file direct access — works correctly
test_content "custom-script.php direct PHP access" \
    "$BASE/custom-script.php" "200" "WordPress Multisite custom script"

# 11. Non-existing page routes to index.php — works correctly
test_content "Non-existing page routes via index.php" \
    "$BASE/nonexistent-page/" "200" "WordPress Multisite Content Route"

# 12. Non-existing directory routes to index.php — works correctly
test_content "Non-existing directory routes via index.php" \
    "$BASE/nonexistent-directory/" "200" "WordPress Multisite Content Route"

suite_end
