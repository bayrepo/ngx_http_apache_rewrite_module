# WordPress .htaccess Test Structure

## Directory Layout Overview

```
/test1/cms/wordpress/
├── existing.jpg       - Existing file for routing test (200 OK)
│                      - Tests: RewriteRule !-f condition passes for existing files
├── index.php          - WordPress entry point
│                      - Returns: "WordPress Content Route" page
│                      - Used by Rule 3 (direct access protection) and Rule 4 (routing)
├── README.md          - This documentation file
├── somedir/           - Existing directory for routing test (200 OK)
│                      - Tests: RewriteRule !-d condition passes for existing directories
└── test-wp-rewriterules.sh - Bash script to test all WordPress rules using curl
```

## Apache Rules Explained

### 1. HTTP Authorization Header Passing

```apache
RewriteEngine On
RewriteRule .* - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
```

**Что делает:** Копирует HTTP Authorization header из запроса в переменную `HTTP_AUTHORIZATION` для PHP

**Зачем нужно:** WordPress REST API использует это для аутентификации через Bearer token

**Пример использования:**
```bash
curl -H "Authorization: Bearer secret_token" http://site.com/wp-json/wp/v2/posts
```

### 2. RewriteBase Configuration

```apache
RewriteBase /
```

**Что делает:** Устанавливает базовый путь переписывания к корню сайта

**Зачем нужно:** Определяет контекст для относительных путей в других правилах

### 3. Direct index.php Access Protection

```apache
RewriteRule ^index\.php$ - [L]
```

**Что делает:** Если запрос идёт прямо на `index.php`, ничего не переписывает, останавливает процесс ([L] = Last)

**Зачем нужно:** Предотвращает зацикливание маршрутизации

### 4. WordPress Core Routing

```apache
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule . /index.php [L]
```

**Что делает:**
- `!-f` - проверяет, что запрашиваемый файл НЕ существует
- `!-d` - проверяет, что запрашиваемая директория НЕ существует  
- Если оба условия true - маршрутизирует через `/index.php`

**Зачем нужно:** WordPress использует "чистые URL" (permalinks), все запросы на несуществующие файлы/директории рутятся через index.php для обработки WP core

## Test Script Features

The script includes two test functions:
1. `test_rule()` - checks HTTP status code only
2. `test_rule_content()` - checks both status AND response body content

## Test Coverage Summary

### 1. HTTP_AUTHORIZATION Header ✓
- REST API request with Authorization header → **200** + "WordPress Content Route" ✓

### 2. RewriteBase Configuration ✓
- Root directory access → **200** + "WordPress Content Route" ✓

### 3. Direct index.php Access ✓
- `/index.php` direct call → **200** (not rewritten) ✓

### 4. WordPress Core Routing ✓
- Non-existing file routes to `index.php` → **200** ✓
- Existing file access (`existing.jpg`) → **200** ✓
- Non-existing directory routes to `index.php` → **200** ✓
- Existing directory access (`somedir/`) → **200** ✓

## Run Tests

Execute the test script to verify all rules:
```bash
cd /home/alexey/projects/workspace-zed/test1/cms/wordpress
./test-wp-rewriterules.sh
```

Expected results (all should be **PASS ✓**):
- Authorization header handling: HTTP 200 + content "WordPress Content Route" ✓
- Base path routing: HTTP 200 + content "WordPress Content Route" ✓
- Direct index.php access: HTTP 200 ✓
- Non-existing file routing: HTTP 200 ✓
- Existing file access: HTTP 200 ✓
- Non-existing directory routing: HTTP 200 ✓
- Directory access: HTTP 200 ✓
