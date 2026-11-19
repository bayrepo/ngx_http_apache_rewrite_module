#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

# Test InheritBefore option using the simple site (port 8081)
# The simple site has no location-level rules in nginx.conf,
# so we test via .htaccess RewriteOptions InheritBefore behavior.
#
# Since InheritBefore affects rule ordering between htaccess and location rules,
# and the simple site only has htaccess rules, this test verifies that
# the option is parsed without errors and doesn't break normal operation.

BASE="http://localhost:8081"

suite_start "InheritBefore option — port 8081"

# Verify basic functionality still works with InheritBefore parsing
test_redirect ".xmx redirect still works with InheritBefore" \
    "$BASE/test.xmx" "301" "/index.html"

test_status "stop.html still blocked (403)" \
    "$BASE/stop.html" "403"

test_content "redirect.html → show.html still works" \
    "$BASE/redirect.html" "200" "show.html"

test_status "Existing index.html returns 200" \
    "$BASE/index.html" "200"

suite_end
