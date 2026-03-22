# WordPress Multisite Test Structure - Documentation

## Directory Layout Overview

```
/test1/cms/wordpress-multy/
├── index.php                  - WordPress entry point (routes non-existing files)
│                                - Returns: "WordPress Multisite Content Route" page
├── wp-admin/                  - Admin directory tests
│   └── (empty directory for trailing slash redirect test)
├── wp-admin/admin.php         - Test file for wp-admin routing rule
├── wp-content/                - WordPress content directory tests
│   └── themes/                - Theme subdirectory
├── wp-includes/               - Core files directory tests  
│   └── js/                    - JavaScript subdirectory
│   └── (empty for testing)
├── style.css                  - Existing CSS file (tests RewriteRule !-f condition)
├── script.js                  - Existing JS file (tests RewriteRule !-f condition)
├── test-wp-multi-rewriterules.sh - Bash script to test all rules using curl
└── README.md                  - This documentation file
```

## Apache Rules Explained - WordPress Multisite

### 1. HTTP Authorization Header Passing

```apache
RewriteRule .* - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
```

**Что делает:** Копирует HTTP Authorization header в переменную HTTP_AUTHORIZATION для PHP

### 2. RewriteBase Configuration

```apache
RewriteBase /
```

**Что делает:** Устанавливает базовый путь к корню сайта

### 3. Direct index.php Access Protection

```apache
RewriteRule ^index\.php$ - [L]
```

**Что делает:** Предотвращает зацикливание маршрутизации (не переписывает прямой запрос на index.php)

### 4. wp-admin Trailing Slash Redirect

```apache
# add a trailing slash to /wp-admin
RewriteRule ^([_0-9a-zA-Z-]+/)?wp-admin$ $1wp-admin/ [R=301,L]
```

**Что делает:** Добавляет слэш в конце к URL `/wp-admin`, редирект 301 (Moved Permanently)

**Зачем нужно:** WordPress multisite требует trailing slash для admin panel, обеспечивает корректную маршрутизацию

**Подхват поддомена:** `[_0-9a-zA-Z-]+/?` позволяет обработку поддоменов в multisite setup:
- `/wp-admin` → редирект на `/wp-admin/` (301)
- `site.subdomain.com/wp-admin` → редирект на `site.subdomain.com/wp-admin/`

### 5. Existing Files/Directories Check (Skip Processing)

```apache
RewriteCond %{REQUEST_FILENAME} -f [OR]
RewriteCond %{REQUEST_FILENAME} -d
RewriteRule ^ - [L]
```

**Что делает:** Если файл ИЛИ директория СУЩЕСТВУЕТ - останавливаем обработку правил, пропускаем запрос напрямую

**Зачем нужно:** Предотвращает маршрутизацию на index.php для существующих статических файлов (CSS, JS, изображения)

### 6. WordPress Multisite Core Routing Rules

```apache
RewriteRule ^([_0-9a-zA-Z-]+/)?(wp-(content|admin|includes).*) $2 [L]
```

**Что делает:** Для multisite setups - маршрутизирует wp-content, wp-admin, wp-includes напрямую (без index.php)

**Подхват поддоменов:** `[_0-9a-zA-Z-]+/?` позволяет обработку поддоменов в multisite:
- `/wp-content/uploads/image.jpg` → прямой доступ к файлу
- `site.subdomain.com/wp-admin/admin.php` → прямая маршрутизация к admin.php

### 7. All PHP Files Direct Access

```apache
RewriteRule ^([_0-9a-zA-Z-]+/)?(.*\.php)$ $2 [L]
```

**Что делает:** Любой PHP файл отдаётся напрямую, без маршрутизации через index.php

**Подхват поддоменов:** `[_0-9a-zA-Z-]+/?` позволяет обработку поддоменов в multisite:
- `/wp-login.php` → прямой доступ к файлу
- `site.subdomain.com/wp-signup.php` → прямая маршрутизация к signup.php

### 8. Final WordPress Routing Rule

```apache
RewriteRule . index.php [L]
```

**Что делает:** Все оставшиеся запросы маршрутизируются через index.php (основной URL routing)

## Test Script Features

The script includes test functions:
1. **test_rule()** - checks HTTP status code only
2. **test_rule_content()** - checks both status AND response body content

## Multisite-Specific Testing Scenarios

### Subdomain vs Subdirectory Setup

WordPress Multisite может быть configured в двух режимах:
- **Subdomain mode**: sites поддоменами (site1.example.com, site2.example.com)
- **Subdirectory mode**: сайты поддиректориями (/site1/, /site2/)

Правило `[_0-9a-zA-Z-]+/?` обрабатывает оба случая:
- Для subdomain: `[subdomain/]?` = optional prefix
- Для subdirectory: `[directory/]?` = optional prefix

### Test Examples

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/wp-admin` | trailing slash redirect | 301 → `/wp-admin/` ✓ |
| `http://test.my.brp/wp-admin/` | direct access (directory exists) | 200 OK ✓ |
| `http://test.my.brp/style.css` | existing file (!-f passes) | 200 OK ✓ |
| `http://test.my.brp/script.js` | existing file (!-f passes) | 200 OK ✓ |
| `http://test.my.brp/wp-admin/admin.php` | multisite wp-admin rule | 200 OK ✓ |
| `http://test.my.brp/wp-content/uploads/test.jpg` | multisite wp-content rule | 200 OK ✓ |
| `http://test.my.brp/nonexistent-page/` | final index.php routing | 200 OK (content from index.php) ✓ |

## Run Tests

Execute the test script to verify all rules:
```bash
cd /home/alexey/projects/workspace-zed/test1/cms/wordpress-multy
./test-wp-multi-rewriterules.sh
```

Expected results for WordPress Multisite tests (all should be **PASS ✓**):
- wp-admin trailing slash redirect: HTTP 301 ✓
- wp-admin directory access: HTTP 200 ✓
- Existing file (CSS/JS) access: HTTP 200 ✓
- Existing directory access: HTTP 200 ✓
- WordPress multisite routing (wp-content, admin): HTTP 200 ✓
- Final index.php routing: HTTP 200 + content "WordPress Multisite Content Route" ✓
