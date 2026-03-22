# Joomla .htaccess Test Structure

## Directory Layout Overview

```
/test1/cms/joomla/
├── api/                      - API directory tests
│   └── index.php             - Joomla API entry point (routes /api/ requests)
│                               - Returns: "Joomla API Configuration Loaded"
├── .well-known/              - Well-known directory
│   └── robots.txt            - Allowed file via exception
├── base64-test.php           - Security test for base64_encode pattern detection
├── globals-test.php          - Security test for GLOBALS exploitation pattern
├── request-test.php          - Security test for _REQUEST manipulation pattern
├── script-test.php           - Security test for script injection pattern
├── index.php                 - Joomla main entry point (routes non-existing files)
│                                - Returns: "Joomla Content Route" page
├── somedir/                  - Directory for testing !-d condition (200 OK)
├── test-joomla-rewriterules.sh - Bash script to test all rules using curl
└── README.md                 - This documentation file
```

## Apache Rules Explained - Joomla

### 1. Base64 Encoded Payload Detection Rule

```apache
RewriteCond %{QUERY_STRING} base64_encode[^(]*\([^)]*\) [OR]
```

**Что делает:** Detects Base64 encoded payloads in query string (function call pattern)
**Зачем нужно:** Защита от Base64-encoded malicious code injection attacks
- Pattern: `base64_encode(...)` - detect function calls that encode data

### 2. Script Injection Pattern Detection Rule

```apache
RewriteCond %{QUERY_STRING} (<|%3C)([^s]*s)+cript.*(>|%3E) [NC,OR]
```

**Что делает:** Detects script injection patterns (HTML entities decoded)
**Зачем нужно:** Защита от XSS attacks через URL parameters
- Pattern: `<script>...` or `%3Cscript%3E` - detect HTML script tags
- `[NC]` - case-insensitive matching

### 3. GLOBALS Exploitation Detection Rule

```apache
RewriteCond %{QUERY_STRING} GLOBALS(=|\[|\%[0-9A-Z]{0,2}) [OR]
```

**Что делает:** Detects GLOBALS exploitation attempt in query string
**Зачем нужно:** Защита от PHP superglobal abuse attacks
- Pattern: `GLOBALS=` or `GLOBALS[` - detect attempts to manipulate global variables

### 4. _REQUEST Manipulation Detection Rule

```apache
RewriteCond %{QUERY_STRING} _REQUEST(=|\[|\%[0-9A-Z]{0,2})
```

**Что делает:** Detects _REQUEST manipulation (PHP superglobal abuse)
**Зачем нужно:** Защита от PHP superglobal exploitation attacks
- Pattern: `_REQUEST=` or `_REQUEST[` - detect attempts to manipulate $_REQUEST array

### 5. Malicious Requests Blocked with [F] Flag

```apache
RewriteRule .* index.php [F]
```

**Что делает:** Blocks malicious requests with 403 Forbidden
**Зачем нужно:** Все запросы, которые прошли security checks выше - блокируются

### 6. HTTP Authorization Header Passing Rule

```apache
RewriteRule .* - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
```

**Что делает:** Копирует Authorization header в переменную HTTP_AUTHORIZATION для PHP
**Зачем нужно:** Joomla REST API требует эту переменную для аутентификации

### 7. Joomla API Routing Rule (/api/)

```apache
RewriteCond %{REQUEST_URI} ^/api/
RewriteCond %{REQUEST_URI} !^/api/index\.php
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule .* api/index.php [L]
```

**Что делает:** Маршрутизирует запросы в `/api/` через `api/index.php` (если не существующий файл/directory)
**Зачем нужно:** Joomla REST API routing - отдельная точка входа для API endpoints
- **Исключение:** Если запрос прямо на /api/index.php - пропускаем (не переписываем)

### 8. Main Joomla Routing Rule (/index.php exclusion)

```apache
RewriteCond %{REQUEST_URI} !^/index\.php
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule .* index.php [L]
```

**Что делает:** Маршрутизирует все запросы через main `index.php` (!-f AND !-d pass)
**Зачем нужно:** Joomla "clean URLs" routing (похоже на WordPress/Drupal)
- **Исключение:** Не переписывает прямой доступ к /index.php

## Test Script Features

The script includes test functions:
1. **test_rule()** - checks HTTP status code only
2. **test_rule_content()** - checks both status AND response body content

## Security Pattern Testing Scenarios

### Query String Pattern Detection

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/base64-test.php?data=base64_encode(secret)` | base64_encode pattern detection → 403 ✓ |
| `http://test.my.brp/script-test.php?q=%3Cscript%3Ealert(1)%3E` | script injection pattern → 403 ✓ |
| `http://test.my.brp/globals-test.php?GLOBALS[user]=admin` | GLOBALS exploitation → 403 ✓ |
| `http://test.my.brp/request-test.php?_REQUEST[config]=true` | _REQUEST manipulation → 403 ✓ |

### API vs Main Routing

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/api/` | api/index.php routing (!-f, !-d pass) → api/index.php ✓ |
| `http://test.my.brp/api/index.php` | Direct access (excluded from api routing) → 200 OK ✓ |
| `http://test.my.brp/nonexistent-page/` | Main index.php routing (!-f, !-d pass) → main index.php ✓ |

## Run Tests

Execute the test script to verify all rules:
```bash
cd /home/alexey/projects/workspace-zed/test1/cms/joomla
./test-joomla-rewriterules.sh
```

Expected results for Joomla tests (all should be **PASS ✓**):
- Base64 encoded payload blocked: HTTP 403 ✓
- Script injection blocked: HTTP 403 ✓
- GLOBALS exploitation blocked: HTTP 403 ✓
- _REQUEST manipulation blocked: HTTP 403 ✓
- Authorization header handling: HTTP 200 + "Joomla Content Route" ✓
- API index.php routing: HTTP 200 + "Joomla API Configuration Loaded" ✓
- Direct /api/index.php file access: HTTP 200 OK ✓
- Main index.php routing (non-existing page): HTTP 200 + "Joomla Content Route" ✓
- Directory access (!-d): HTTP 200 OK ✓
