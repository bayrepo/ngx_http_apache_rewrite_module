# Drupal .htaccess Test Structure

## Directory Layout Overview

```
/test1/cms/drupal/
├── core/                     - Drupal core directory tests
│   ├── install.php           - Install script (protected by RewriteRule ^core/install\.php)
│   ├── rebuild.php           - Rebuild script (protected by RewriteRule ^core/rebuild\.php)
│   └── modules/system/tests/ - Tests directory with https/http.php files
│       ├── https.php         - Test HTTPS test file (excluded from routing)
│       └── http.php          - Test HTTP test file (excluded from routing)
├── favicon.ico               - Existing favicon for !-f condition test
├── index.php                 - Drupal entry point (routes non-existing files)
│                                - Returns: "Drupal Content Route" page
├── .htaccess                 - Hidden .htaccess file (blocked by hidden files rule)
├── .htpasswd                 - Hidden .htpasswd file (blocked by hidden files rule)
├── .well-known/              - Well-known directory (allowed exception)
│   └── robots.txt            - Allowed file via exception (?!well-known)
├── somedir/                  - Directory for testing !-d condition (200 OK)
├── test-drupal-rewriterules.sh - Bash script to test all rules using curl
└── README.md                 - This documentation file
```

## Apache Rules Explained - Drupal

### 1. RewriteEngine Activation

```apache
RewriteEngine on
```

**Что делает:** Включает модуль mod_rewrite для этого каталога
**Зачем нужно:** Без этого все правила rewrite не работают

### 2. Protocol Variables (HTTP/HTTPS Detection)

```apache
RewriteRule ^ - [E=protossl]
RewriteCond %{HTTPS} on
RewriteRule ^ - [E=protossl:s]
```

**Что делает:** Устанавливает переменную окружения `protossl` = "https" или "http" в зависимости от HTTPS status
**Зачем нужно:** Drupal использует это для генерации правильных ссылок (http vs https)
- Если HTTPS off → protossl = "" (пусто, http)
- Если HTTPS on → protossl = "s"

### 3. HTTP Authorization Header Passing

```apache
RewriteRule ^ - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
```

**Что делает:** Копирует Authorization header в переменную HTTP_AUTHORIZATION для PHP
**Зачем нужно:** Drupal REST API требует эту переменную для аутентификации

### 4. Hidden Files/Patterns Protection Rule

```apache
RewriteRule "/\.|^\.(?!well-known/)" - [F]
```

**Что делает:** Блокирует (403 Forbidden) файлы начинающиеся с точки, кроме .well-known/
- `/\.` - блокирует /filename.хотя бы одна точка в path
- `^\.(?!well-known/)` - блокирует /隐藏文件, но исключает /well-known/

**Зачем нужно:** Защита от доступа к скрытым файлам (.htaccess, .htpasswd, .git)
**Исключение:** .well-known/robots.txt разрешён (для SEO и security)

### 5. Core install/rebuild.php Protection Rules

```apache
RewriteCond %{REQUEST_URI} ^(.*)?/(install\.php) [OR]
RewriteCond %{REQUEST_URI} ^(.*)?/(rebuild\.php)
RewriteCond %{REQUEST_URI} !core
RewriteRule ^ %1/core/%2 [L,QSA,R=301]
```

**Что делает:** Редирект 301 с /install.php → /core/install.php, /rebuild.php → /core/rebuild.php
**Зачем нужно:** Перемещает install/rebuild скрипты в core directory для безопасности

### 6. Core install.php Rewrite

```apache
RewriteRule ^core/install\.php core/install.php?rewrite=ok [QSA,L]
```

**Что делает:** Переписывает запрос на core/install.php с добавлением параметра rewrite=ok
**Зачем нужно:** Drupal internal handling для процесса установки

### 7. Main Drupal Routing Rules (!-f, !-d)

```apache
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteCond %{REQUEST_URI} !=/favicon.ico
RewriteRule ^ index.php [L]
```

**Что делает:** Маршрутизирует через index.php все запросы на несуществующие файлы И директории, кроме favicon.ico
**Зачем нужно:** "Чистые URL" Drupal (похоже на WordPress), routing через index.php

### 8. Core Modules Tests Files Exceptions

```apache
RewriteCond %{REQUEST_URI} !/core/[^/]*\.php$
RewriteCond %{REQUEST_URI} !/core/modules/system/tests/https?\.php
```

**Что делает:** Исключает из routing core/*.php и tests/https.php/http.php файлы
**Зачем нужно:** Эти файлы должны обрабатываться напрямую, не через main index.php

## Test Script Features

The script includes test functions:
1. **test_rule()** - checks HTTP status code only
2. **test_rule_content()** - checks both status AND response body content

## Multisite-Specific Testing Scenarios

### Hidden Files Protection

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/.htaccess` | hidden files rule `/\.|^\.(?!well-known/)` | 403 Forbidden ✓ |
| `http://test.my.brp/.htpasswd` | hidden files rule | 403 Forbidden ✓ |
| `http://test.my.brp/.well-known/robots.txt` | well-known exception | 200 OK (allowed) ✓ |

### Core install/rebuild.php Protection

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/install.php` | RewriteRule ^(.*)?/(install\.php) + core exclusion | 301 → /core/install.php ✓ |
| `http://test.my.brp/rebuild.php` | RewriteRule ^(.*)?/(rebuild\.php) + core exclusion | 301 → /core/rebuild.php ✓ |

### Core Files Routing Exceptions

| URL | Правило | Ожидаемый результат |
|-----|---------|---------------------|
| `http://test.my.brp/core/install.php` | RewriteRule ^core/install\.php ... rewrite=ok parameter | 200 OK ✓ |
| `http://test.my.brp/core/modules/system/tests/https.php` | tests exception !-https?\.php condition | 200 OK ✓ |
| `http://test.my.brp/core/modules/system/tests/http.php` | tests exception !-https?\.php condition (s matches empty) | 200 OK ✓ |

## Run Tests

Execute the test script to verify all rules:
```bash
cd /home/alexey/projects/workspace-zed/test1/cms/drupal
./test-drupal-rewriterules.sh
```

Expected results for Drupal tests (all should be **PASS ✓**):
- Basic page routing via index.php: HTTP 200 + "Drupal Content Route" ✓
- Hidden files (.htaccess, .htpasswd) blocked: HTTP 403 ✓
- .well-known/robots.txt allowed: HTTP 200 ✓
- install.php redirect to core: HTTP 301 ✓
- rebuild.php redirect to core: HTTP 301 ✓
- favicon.ico direct access (!-f): HTTP 200 ✓
- Non-existing page routing to index.php: HTTP 200 ✓
- Directory access (!-d): HTTP 200 ✓
- Tests files https.php/http.php excluded from routing: HTTP 200 ✓
