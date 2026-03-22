# Grav CMS Test Structure - Updated

## Directory Layout Overview

```
/test1/cms/grav/
├── .git/              - Git folder tests (blocked)
│   └── secret.txt
├── .well-known/       - Well-known directory (allowed exception)
│   └── robots.txt
├── .htaccess          - Main Apache rules configuration
├── .htpasswd          - Hidden file (blocked by Rule 5)
├── bin/               - Bin folder tests (blocked)
│   └── helper.php
├── backup/            - Backup folder tests (blocked)
│   └── archive.zip
├── cache/             - Cache folder tests (blocked)
│   └── test.txt
├── composer.json      - Config file protection (blocked)
├── composer.lock      - Config file protection (blocked)
├── existing.jpg       - Existing file for routing test (200 OK)
├── index.php          - Grav CMS entry point - routes non-existing files
│                      - Returns: "Grav CMS Content Route" page
├── LICENSE.txt        - Config file protection (blocked)
├── logs/              - Logs folder tests (blocked)
│   └── app.log
├── normal-page.md     - Normal Grav CMS page (routes through index.php)
├── README.md          - This documentation file
├── somedir/           - Empty directory for routing test (200 OK)
├── system/            - System folder tests (blocked extensions)
│   └── config.xml
├── vendor/            - Vendor folder tests (blocked extensions)
│   └── module.txt
├── user/              - User folder tests (blocked extensions)
│   ├── test.txt
│   ├── data.json
│   ├── template.twig
│   ├── script.sh
│   ├── module.php
│   ├── config.yml
│   └── settings.yaml
├── webserver-configs/ - Webserver configs folder tests (blocked)
│   └── nginx.conf
├── tests/             - Tests folder tests (blocked)
│   └── unit-test.php
├── test-mustache.php  - Security: Mustache template injection pattern
├── twig-test.html     - Security: Twig syntax injection pattern
├── test-rewriterules.sh - Bash script to test all rules using curl
└── README.md          - Documentation
```

## Updated Test Script Features

### New Content Verification Function

The script now includes a `test_rule_content()` function that:
1. Checks HTTP status code matches expected value
2. Verifies response body contains expected content string

**Example usage:**
```bash
test_rule_content "Normal page routing via index.php" \
    "$BASE_URL/normal-page/" \
    "200" \
    "Grav CMS Content Route"
```

This tests that:
- URL `/home/alexey/projects/workspace-zed/test1/cms/grav/normal-page/` (non-existing file) 
- Returns HTTP 200 status (routed through index.php via Rule 2)
- Response body contains "Grav CMS Content Route" text from index.php

## Test Coverage Summary

### 1. Security Rules - Malicious Patterns ✓
- Template injection: `{{ }}`, `{% %}` in URI/Query String → **403**
- Base64 payloads: `base64_encode()` pattern → **403**
- Script injection: `<script>` or `%3C` encoded → **403**
- GLOBALS exploitation: `GLOBALS[` pattern → **403**
- _REQUEST manipulation: `_REQUEST[` pattern → **403**

### 2. Core Routing Rules ✓ (Updated)
- Non-existing file routes to index.php → **200** + content check
- Existing file access → **200**
- Existing directory access → **200**

### 3. Sensitive Directory Protection ✓
- Blocks: `.git`, `cache`, `bin`, `logs`, `backup` folders → **403**
- Blocks system/vendor with sensitive extensions (`.txt`, `.xml`) → **403**

### 4. File Extension Protection ✓
- Blocks raw `.md` files → **403**

### 5. Hidden Files/Directories Protection ✓
- Blocks hidden files except `.well-known` → **403** / **200**

### 6. Critical Configuration File Protection ✓
- Blocks: `LICENSE.txt`, `composer.lock`, `composer.json`, `.htaccess` → **403**

## Run Tests

Execute the test script to verify all rules:
```bash
cd /home/alexey/projects/workspace-zed/test1/cms/grav
./test-rewriterules.sh
```

Expected results for security tests (all should be **PASS ✓**):
- Template injection: HTTP 403 ✓
- Twig injection: HTTP 403 ✓
- Base64 payload: HTTP 403 ✓
- Script injection: HTTP 403 ✓
- GLOBALS manipulation: HTTP 403 ✓
- _REQUEST manipulation: HTTP 403 ✓

Expected results for routing tests (should be **PASS ✓**):
- Normal page routing: HTTP 200 + content "Grav CMS Content Route" ✓
- Existing file access: HTTP 200 ✓
- Directory access: HTTP 200 ✓
