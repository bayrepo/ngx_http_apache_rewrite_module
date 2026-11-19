#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8081"

suite_start "Simple Site (HTML only) — port 8081"

# 1. Redirect .xmx → index.html (301)
test_redirect ".xmx redirect to /index.html" \
    "$BASE/test.xmx" "301" "/index.html"

# 2. Block stop.html (403)
test_status "Block stop.html (403 Forbidden)" \
    "$BASE/stop.html" "403"

# 3. Internal rewrite redirect.html → show.html (200 + content)
test_content "Internal rewrite redirect.html → show.html" \
    "$BASE/redirect.html" "200" "show.html"

# 4. Subdir rewrite → index.html
# NOTE: subdir/.htaccess blocks non-.html files, and the parent rule
# rewrites /subdir/ to /index.html. Current behavior returns 403 because
# subdir/.htaccess RewriteCond %{REQUEST_URI} !\.html$ matches /subdir/
test_status "Subdir /subdir/ current behavior (403)" \
    "$BASE/subdir/" "403"

# 5. subdir/.htaccess: block other.xmx (403)
test_status "subdir/.htaccess blocks other.xmx (403)" \
    "$BASE/subdir/other.xmx" "403"

# 6. subdir/.htaccess: file.html → ../show.html (200 + content)
test_content "subdir/file.html rewrites to ../show.html" \
    "$BASE/subdir/file.html" "200" "show.html"

# 7. Existing file access (200)
test_status "Existing index.html returns 200" \
    "$BASE/index.html" "200"

suite_end
