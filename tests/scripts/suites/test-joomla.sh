#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

BASE="http://localhost:8084"

suite_start "Joomla — port 8084"

# 1. Security rules (QUERY_STRING conditions) — now work after server-level engine fix
test_status "base64_encode payload blocked (403)" \
    "$BASE/base64-test.php?data=base64_encode(secret_password)" "403"

test_status "script injection blocked (403)" \
    "$BASE/script-test.php?q=%3Cscript%3Ealert(1)%3E" "403"

test_status "GLOBALS exploitation blocked (403)" \
    "$BASE/globals-test.php?GLOBALS%5Buser%5D=admin" "403"

test_status "_REQUEST manipulation blocked (403)" \
    "$BASE/request-test.php?_REQUEST%5Badmin%5D=true" "403"

# 2. Authorization header passthrough — works correctly
test_content "Authorization header passed to API" \
    "$BASE/rest/api/v1" "200" "Joomla Content Route" \
    "Authorization: Bearer joomla_token_abc"

# 3. API routing (/api/) — works correctly
test_content "API /api/ routes to api/index.php" \
    "$BASE/api/" "200" "success" \
    "Authorization: secret_token_123"

# 4. Direct /api/index.php access
# NOTE: api/index.php checks auth token and returns 401 without it
test_content "Direct /api/index.php without auth returns 401" \
    "$BASE/api/index.php" "401" "unauth"

# 5. Non-existing page routes to main index.php — works correctly
test_content "Non-existing page routes via index.php" \
    "$BASE/nonexistent-page/" "200" "Joomla Content Route"

# 6. index.php/component/config access — works correctly
test_content "/index.php/component/config routes correctly" \
    "$BASE/index.php/component/config?view=modules" "200" "Joomla Content Route"

suite_end
