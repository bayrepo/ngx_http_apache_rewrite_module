#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8085"

suite_start "Grav CMS — port 8085"

# 1. Security patterns (QUERY_STRING conditions) — now work after server-level engine fix
test_status "Template injection blocked (403)" \
    "$BASE/test-mustache.php?\{\{config.secret\}\}" "403"

test_status "Base64 payload blocked (403)" \
    "$BASE/test.php?data=base64_encode(some_secret)" "403"

# 2. Core routing: normal page via index.php
test_content "Normal page routing via index.php" \
    "$BASE/normal-page/" "200" "Grav CMS Content Route"

# 3. Existing file/directory access — negative lookahead now works
test_status "existing.jpg accessible (200)" \
    "$BASE/existing.jpg" "200"

# somedir/ exists but has no index file, nginx returns 403 (autoindex off)
test_status "somedir/ no index file (403)" \
    "$BASE/somedir/" "403"

# 4. Sensitive directories
test_status ".git/secret.txt falls back to index.php (200)" \
    "$BASE/.git/secret.txt" "200"

test_status "cache folder blocked (403)" \
    "$BASE/cache/test.txt" "403"

# bin folder now correctly blocked after server-level engine fix
test_status "bin folder blocked (403)" \
    "$BASE/bin/helper.php" "403"

test_status "backup folder blocked (403)" \
    "$BASE/backup/archive.zip" "403"

# 5. System/vendor config files
test_status "system/config.xml blocked (403)" \
    "$BASE/system/config.xml" "403"

test_status "vendor/module.txt blocked (403)" \
    "$BASE/vendor/module.txt" "403"

# 6. Raw .md file
test_status "Raw .md file blocked (403)" \
    "$BASE/test.md" "403"

# 7. Hidden files
test_status ".htpasswd blocked (403)" \
    "$BASE/.htpasswd" "403"

# .well-known now allowed (negative lookahead works)
test_status ".well-known/robots.txt allowed (200)" \
    "$BASE/.well-known/robots.txt" "200"

# 8. Critical config files
test_status "LICENSE.txt blocked (403)" \
    "$BASE/LICENSE.txt" "403"

test_status "composer.lock blocked (403)" \
    "$BASE/composer.lock" "403"

test_status "composer.json blocked (403)" \
    "$BASE/composer.json" "403"

test_status ".htaccess blocked (403)" \
    "$BASE/.htaccess" "403"

# 9. Hierarchical .htaccess merge not yet implemented.
# When subdir has its own .htaccess, root security rules are NOT applied.
# This is a known limitation — see PLAN.md for implementation plan.
test_status "Hierarchical: user/data/*.json NOT blocked (no merge yet) (200)" \
    "$BASE/user/data/test-probe.json" "200"

suite_end
