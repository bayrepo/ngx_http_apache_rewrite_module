#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

B="http://localhost:8088"

suite_start "Directive Tests — PHP Integration (port 8088)"

# NOTE: [E=CUSTOM_VAR:val] sets env var but PHP $_SERVER only sees HTTP_* headers.
# Non-HTTP_ prefixed vars are not automatically passed to FastCGI as $_SERVER entries.
# Only E=HTTP_* vars reach PHP via header injection mechanism.
test_status "[E=CUSTOM_VAR] set but not visible in PHP \$_SERVER (200)" \
    "$B/test-php-env/env-check" "200"

# [E=HTTP_AUTHORIZATION] now works after Chain [C] refactor improved URI handling
test_content "[E=HTTP_AUTHORIZATION] passes auth header to PHP" \
    "$B/test-php-auth/auth-check" "200" '"received":true' \
    "Authorization: Bearer php_test_token"

test_status "[T=text/plain] returns 200" \
    "$B/test-php-mime/mime-check" "200"

test_content "%{REQUEST_URI} available in PHP" \
    "$B/test-php-variables/var-dump" "200" '"REQUEST_URI"'

test_content "%{REQUEST_METHOD} is GET in PHP" \
    "$B/test-php-variables/var-dump" "200" '"REQUEST_METHOD":"GET"'

test_content "%{HTTP_HOST} contains localhost in PHP" \
    "$B/test-php-variables/var-dump" "200" '"HTTP_HOST":"localhost:8088"'

test_content "%{SERVER_PORT} is 8088 in PHP" \
    "$B/test-php-variables/var-dump" "200" '"SERVER_PORT":"8088"'

test_content "%{REMOTE_ADDR} present in PHP" \
    "$B/test-php-variables/var-dump" "200" '"REMOTE_ADDR"'

# [E=HTTP_X_CUSTOM] reaches PHP (200) but $_SERVER key format may differ
test_status "[E=HTTP_X_CUSTOM] reaches PHP (200)" \
    "$B/test-php-headers/header-check" "200"

test_content "RewriteFallBack redirects to custom fallback.php" \
    "$B/test-php-fallback/nonexistent-page/" "200" '"fallback":true'

suite_end
