# Integration Tests

## Overview

Integration tests verify the `.htaccess` mod_rewrite module behavior against real CMS configurations. Each test suite runs inside a Docker container with nginx + php-fpm and validates HTTP responses (status codes, response body content, redirect headers) using curl.

Tests fix the **current module behavior as baseline**. Known limitations are documented inline in each test script with `NOTE:` comments. When a module bug is fixed, the corresponding test expectation should be updated.

## Prerequisites

- Docker Engine (20.10+)
- Docker Compose v2 (`docker compose`)
- Internet access (for pulling `almalinux:9` base image and downloading nginx source on first build)

## Quick Start

```bash
cd tests
docker compose up --build --abort-on-container-exit
```

This will:
1. Build the module from local source (`modules/mod_rewrite/`) against system nginx from AlmaLinux 9
2. Start nginx + php-fpm with 6 test sites
3. Run all test suites sequentially
4. Print results and exit with code 0 (all pass) or 1 (any fail)

### Re-run without rebuild

If only test scripts changed (not module source):

```bash
docker compose up --abort-on-container-exit
```

### Clean up

```bash
docker compose down -v
```

### Run against Apache + mod_rewrite (reference)

The same test suites can run against Apache httpd + mod_rewrite to verify expected behavior:

```bash
cd tests
docker compose -f docker-compose.apache.yml up --build --abort-on-container-exit
```

Failures in Apache mode indicate either:
- **Test expectations are nginx-specific** (documented with `NOTE:` comments) — these show where the module diverges from Apache
- **Apache configuration issue** (500 errors) — needs httpd.conf adjustment
- **Real module bugs** — when Apache passes but nginx module fails on the same `.htaccess`

### Clean up Apache

```bash
docker compose -f docker-compose.apache.yml down -v
```

## Test Environment

| Component | Detail |
|-----------|--------|
| Base OS | AlmaLinux 9 |
| nginx | System package (currently 1.20.1) |
| PHP | System php-fpm + php-cli |
| Module build | `--with-compat --add-dynamic-module` |
| Architecture | x86_64 |

### Container Architecture

Two containers orchestrated by docker compose:

- **build** — compiles `ngx_http_apache_rewrite_module.so` from mounted source, outputs to shared volume
- **test** — runs nginx + php-fpm, mounts the compiled `.so`, site files from `cms/`, and test scripts

Source files (`modules/`, `cms/`) are mounted read-only — changes are picked up on next run without rebuilding the image.

### Site Configuration

Each CMS is served on a dedicated port:

| Port | CMS | Root Path | PHP |
|------|-----|-----------|-----|
| 8081 | Simple | `/sites/simple` | No |
| 8082 | Drupal | `/sites/drupal` | Yes |
| 8083 | WordPress | `/sites/wordpress/wordpress` | Yes |
| 8084 | Joomla | `/sites/joomla` | Yes |
| 8085 | Grav | `/sites/grav` | Yes |
| 8086 | WordPress Multisite | `/sites/wordpress-multy` | Yes |

All sites have `HtaccessEnable on`, `RewriteEngine On`. PHP sites additionally have `FallbackToIndex on` and FastCGI configuration.

## Test Suites

### test-simple.sh (7 tests)
Basic mod_rewrite functionality without PHP:
- External redirect (`.xmx` → `/index.html`, 301)
- Forbidden rule (`stop.html`, 403)
- Internal rewrite (`redirect.html` → `show.html`, 200)
- Subdirectory rewrite rules
- Nested `.htaccess` in `subdir/` (block `.xmx`, rewrite `file.html`)
- Existing file access

### test-drupal.sh (10 tests)
Drupal `.htaccess` rules:
- Hidden file protection (`.htaccess`, `.htpasswd` → 403)
- `.well-known` exception (known limitation: negative lookahead not supported → 403)
- `install.php` / `rebuild.php` redirect (known limitation: backreferences `%1/%2` not supported → routes via index.php)
- `favicon.ico` access (known limitation: dot-pattern regex blocks it)
- Core test file exclusions (`https.php`, `http.php` → 200)
- Authorization header passthrough

### test-wordpress.sh (8 tests)
WordPress `.htaccess` rules:
- `[E=HTTP_AUTHORIZATION]` passthrough to PHP (REST API mock verifies token)
- Unauthorized request → 401
- `RewriteBase /wordpress` routing
- Direct `index.php` access (not rewritten)
- `!-f` / `!-d` conditions for non-existing paths
- Existing file access (`existing.jpg`)
- Directory without index file → 403

### test-joomla.sh (9 tests)
Joomla `.htaccess` rules:
- Security rules using `%{QUERY_STRING}` (known limitation: variable not implemented → requests pass through)
- Authorization header passthrough to API
- `/api/` routing to `api/index.php`
- Direct `api/index.php` auth check (401 without token)
- Main routing for non-existing pages
- `/index.php/component/config` SEF URL routing

### test-grav.sh (18 tests)
Grav CMS `.htaccess` rules (most comprehensive suite):
- Security patterns via `%{QUERY_STRING}` (known limitation)
- Core page routing via `index.php`
- Existing file/directory access (known limitation: dot-pattern blocks some)
- Sensitive directory blocking (`cache/`, `backup/`, `system/`, `vendor/`)
- File extension protection (`.md` → 403)
- Hidden file protection (`.htpasswd` → 403)
- Critical config file blocking (`LICENSE.txt`, `composer.lock`, `composer.json`, `.htaccess`)

### test-wp-multisite.sh (13 tests)
WordPress Multisite `.htaccess` rules:
- Authorization header handling
- `RewriteBase /` root routing
- Direct `index.php` access
- `wp-admin` trailing slash redirect (301)
- Static file access (`style.css`, `script.js`)
- WordPress core path routing (`wp-admin/`, `wp-content/`, `wp-includes/`)
- Custom PHP file direct access
- Non-existing page/directory routing via `index.php`

## Test Output Format

```
============================================================
  WordPress — port 8083
============================================================
  ✓ PASS [REST API: Authorization header passed] HTTP 200, content matched
  ✓ PASS [REST API: no auth returns 401] HTTP 401, content matched
  ✗ FAIL [Some test] Expected: 200, Got: 403

--- Suite Summary ---
  Pass: 7  Fail: 1

============================================================
  TOTAL RESULTS
============================================================
  Total Pass: 64
  Total Fail: 1
============================================================
SOME TESTS FAILED
```

Exit code: `0` = all pass, `1` = any fail.

## Known Module Limitations (documented in tests)

| Limitation | Affected Suites | Impact |
|------------|----------------|--------|
| `%{QUERY_STRING}` regex with escaped chars | Joomla, Grav | Security rules using `\(` `\)` in QUERY_STRING conditions may not match |
| Backreferences `%1`/`%2` in RewriteRule substitution | Drupal | Complex conditional redirects fall through to index.php |
| Integer comparisons `-lt`/`-eq`/`-gt` syntax | Directive tests | Apache uses different argument order for integer comparisons in .htaccess |

## Apache vs Nginx Module: Expected Test Differences

When running the same test suites against Apache + mod_rewrite (`docker compose -f docker-compose.apache.yml`), some tests produce different results. This table documents expected differences:

### Tests that pass on Apache but fail on nginx module

All previously identified differences have been fixed. The module now behaves identically to Apache mod_rewrite for all testable scenarios. Remaining differences are purely architectural (see below).

### Tests that differ due to architectural differences (not fixable)

| Test | Apache Result | Nginx Result | Reason |
|------|--------------|--------------|--------|
| `SERVER_SOFTWARE` variable | Contains "Apache" | Contains "nginx" | Server identity differs |
| `RewriteFallBack` directive | 500 (directive unknown) | 200 (custom directive) | Not an Apache directive; nginx module extension |
| Directory without index file | 403 (Forbidden) | 403 or 404 | Different default behavior for autoindex off |
| `[E=VAR:VAL]` non-HTTP_ vars in PHP `$_SERVER` | Available via `getenv()` | Not available | Apache mod_php is in-process; nginx php-fpm is separate process |

### Tests that now pass identically on both

| Feature | Status |
|---------|--------|
| Negative lookahead `(?!...)` in regex | ✅ Fixed — parser preserves backslashes correctly |
| All redirect flags `[R=301]`, `[R=302]`, `[R=307]`, `[R=permanent]`, etc. | ✅ Identical |
| `[G]` Gone, `[F]` Forbidden, `[L]` Last, `[END]` | ✅ Identical |
| `[NC]` case-insensitive, `[NE]` no-escape | ✅ Identical |
| `[S=N]` Skip, `[C]` Chain, `[QSA]`, `[QSD]` | ✅ Identical |
| `RewriteBase`, `RewriteMap` (tolower/toupper/escape/unescape) | ✅ Identical |
| Variables: `REQUEST_URI`, `HTTP_HOST`, `DOCUMENT_ROOT`, `HTTPS`, `TIME_*`, `SERVER_*`, `REMOTE_*` | ✅ Identical |
| File conditions: `-f`, `-d`, `-s`, `-x` | ✅ Identical |
| String/int comparisons, pattern negation | ✅ Identical |
| `HtaccessName`, `GlobalLocationRewriteEngine`, `InheritBefore` | ✅ Identical |

## Adding New Tests

The test runner auto-discovers all `tests/scripts/suites/test-*.sh` files — no registration needed.

### Available assertion functions

| Function | Arguments | Purpose |
|----------|-----------|---------|
| `test_status` | description, url, expected_code | Check HTTP status only |
| `test_content` | description, url, expected_code, substring, [headers] | Status + body contains substring |
| `test_redirect` | description, url, expected_code, expected_location | Status + Location header match |

### Adding a test on an existing port

Use this when your test fits into an existing site (e.g., adding a new rule to the WordPress or Simple test site).

1. Add test data to the appropriate `cms/<site>/` directory:
   - `.htaccess` rules
   - HTML/PHP response files with identifiable content markers

2. Add assertions to the existing test script in `tests/scripts/suites/test-<site>.sh`:
   ```bash
   # Example: add to test-wordpress.sh
   test_content "New rule routes /custom/ to custom.php" \
       "$BASE/custom/" "200" "Custom Content Marker"
   ```

3. Run tests to verify:
   ```bash
   cd tests && docker compose up --build --abort-on-container-exit
   ```

### Adding a test on a new port

Use this when you need an isolated site with its own nginx server block (e.g., testing a directive that requires server-level configuration like `RewriteMap`, `HtaccessName`, or `GlobalLocationRewriteEngine`).

#### Step 1: Create test data

```bash
mkdir -p cms/test-my-feature/
```

Create `.htaccess` with your rules:
```apache
# cms/test-my-feature/.htaccess
RewriteEngine On
RewriteBase /test-my-feature/
RewriteRule ^my-test$ result.html [L]
```

Create response files with identifiable content:
```html
<!-- cms/test-my-feature/result.html -->
<h1>my-feature-result</h1>
```

For PHP tests, create `.php` files that output JSON:
```php
<?php
header("Content-Type: application/json");
echo json_encode(["key" => $_SERVER["SOME_VAR"] ?? "(not set)"]);
```

#### Step 2: Add nginx server block

Edit `tests/nginx/nginx.conf` and add a new server block **before the closing `}` of the `http` block**:

```nginx
# ============================================================
# My Feature Test — port 80XX
# ============================================================
server {
    listen 80XX;
    server_name localhost;

    root /sites;                          # shared root for all sites
    HtaccessEnable on;
    RewriteEngine On;
    FallbackToIndex off;                  # set on/off as needed

    # Server-level directives if needed:
    # RewriteMap mymap int:tolower;
    # HtaccessName .custom;
    # GlobalLocationRewriteEngine on;

    location / {
        RewriteEngine On;
    }

    # Add PHP location block if testing PHP:
    # location ~* \.php$ {
    #     include fastcgi_params;
    #     fastcgi_pass unix:/run/php-fpm/www.sock;
    #     fastcgi_index index.php;
    #     fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
    # }
}
```

> **Important:** Use `root /sites;` (shared root), not `alias`. The module resolves `.htaccess` paths relative to `root`, and `alias` breaks this resolution. Your test site directory (`cms/test-my-feature/`) is accessed via URI path `/test-my-feature/...`.

#### Step 3: Expose the port

Edit `tests/docker-compose.yml` and update the ports range:
```yaml
ports:
  - "8081-80XX:8081-80XX"
```

#### Step 4: Create the test script

Create `tests/scripts/suites/test-my-feature.sh`:
```bash
#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

B="http://localhost:80XX"

suite_start "My Feature Test — port 80XX"

test_content "My rule works correctly" \
    "$B/test-my-feature/my-test" "200" "my-feature-result"

# For PHP tests with headers:
# test_content "Auth header passed" \
#     "$B/test-my-feature/auth-check" "200" '"received":true' \
#     "Authorization: Bearer token"

suite_end
```

Make it executable:
```bash
chmod +x tests/scripts/suites/test-my-feature.sh
```

#### Step 5: Run and iterate

```bash
cd tests
docker compose up --build --abort-on-container-exit
```

If tests fail, check the actual response:
```bash
# Start container interactively for debugging
docker compose run --rm --entrypoint "sleep 3600" test
# In another terminal:
docker exec -it <container> bash
# Inside container:
php-fpm -D && nginx
curl -sv http://localhost:80XX/test-my-feature/my-test
cat /var/log/nginx/error.log | tail -20
```

### Tips

- **Always use `RewriteBase`** in `.htaccess` when the site is in a subdirectory under shared root. Without it, patterns match against the full URI including the directory prefix.
- **Use identifiable content markers** in response files (e.g., `<h1>unique-marker</h1>`) so `test_content` can verify the correct file was served.
- **Record current behavior as baseline.** If a test reveals a module limitation, document it with a `# NOTE:` comment and set the expected value to the actual behavior. Update the expectation when the bug is fixed.
- **PHP tests need both** a `.htaccess` rewrite rule AND a matching `location ~* \.php$` block in nginx.conf. Without the PHP location block, `.php` files are served as plain text.
- **Server-level directives** (`RewriteMap`, `HtaccessName`, `HtaccessEnable`, `FallbackToIndex`, `GlobalLocationRewriteEngine`) must be in the `server {}` block, not in `location {}`.

## Relationship to `cms/` Directory

The `cms/` directory contains both the original manual test fixtures (Drupal, WordPress, Joomla, Grav, Simple, WordPress Multisite) and newer directive-specific test sites (`test-flag-*`, `test-variable-*`, `test-php-*`, etc.). All test data lives under `cms/` and is mounted read-only into the test container at `/sites/`. The test scripts in `tests/scripts/suites/` reference this data via HTTP requests to the appropriate port and path.
