## Apache mod_rewrite Compatibility Module for Nginx

This module provides Apache's `mod_rewrite` compatibility for Nginx, enabling `.htaccess` file support and standard rewrite rules.

---

# Configuration Directives

The following directives are available in this module:

### 1. RewriteEngine

**Purpose:** Enable or disable the rewrite engine.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `main`, `server`, `location` | ✓ All three levels supported |

**Syntax:**
```nginx
RewriteEngine on|off
```

**Possible Values:**
- `on`  - Enable rewrite engine
- `off` - Disable rewrite engine (default)

**Example in nginx.conf:**
```nginx
http {
    server {
        RewriteEngine on;

        location /blog/ {
            # Rules apply here
        }

        location /static/ {
            RewriteEngine off;  # Disable for this location
        }
    }
}
```

---

### 2. RewriteRule

**Purpose:** Define a rewrite rule that matches a URL pattern and substitutes it with a new value.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `server`, `location`      | ✓ Server and Location level           |
| `.htaccess`               | ✓ Supported in .htaccess files          |

**Syntax:**
```nginx
RewriteRule pattern substitution [flags]
```

**Parameters:**

- **pattern** - Regular expression to match the URL (supports backreferences $1, $2, etc.)
  - Pattern can be prefixed with `!` for negation
  - `-` as substitution means "no substitution" (only match for conditions)

- **substitution** - Target URL or file path to rewrite to
  - Can be a relative path (`/new/path`) or absolute redirect (`http://example.com/new`)

**Flags:** `[flag1,flag2,...]` (optional):

| Flag | Abbreviation | Description |
|------|--------------|-------------|
| `B`  | -            | Escape backreferences in output (prevent URL encoding issues) |
| `C`  | Chain        | Chain this rule with the next one (requires previous match) |
| `D`  | DPI          | Discard path info after substitution |
| `E`  | Env=var:val  | Set an environment variable for later use (FastCGI integration) |
| `F`  | Forbidden    | Return HTTP 403 Forbidden status |
| `G`  | Gone         | Return HTTP 410 Gone status |
| `L`  | Last         | Stop processing rules after this one |
| `N`  | Next=limit   | Loop (retry) up to N times for infinite rewriting prevention |
| `P`  | Proxy        | Proxy the request internally (Phase 2) |
| `Q`  | QSA          | Append original query string to substitution URL |
| `Q`  | QSD          | Discard original query string |
| `Q`  | QSL          | Set "query string last" marker |
| `R`  | Redirect=code| Perform external redirect with status code (301, 302, 307) or: Permanent, Temp, SeeOther |
| `S`  | Skip=N       | Skip N subsequent rules after this one |
| `T`  | Type=mime    | Force specific MIME type for the response |
| `NC` | -            | Case-insensitive matching |
| `NOESCAPE` | -      | Do not escape special characters in output |
| `END`| -            | Stop all rewriting and pass to content phase |

**Example:**
```nginx
# Redirect /old-path/ to /new-path/ (permanent redirect)
RewriteRule ^old-path/(.*)$ /new-path/$1 [R=301,L]

# Condition-based rewrite (chain)
RewriteCond %REQUEST_URI !^/admin/
RewriteRule ^admin/(.*)$ /login.php?user=$1 [NC,E,END]

# In .htaccess:
RewriteEngine on
RewriteCond %{HTTP_HOST} ^www\.example\.com$ [OR]
RewriteCond %{HTTPS} off
RewriteRule ^(.*)$ https://example.com/$1 [R=301,L]
```

---

### 3. RewriteCond

**Purpose:** Define conditions that must be met before a `RewriteRule` is applied. Multiple conditions are ANDed together; OR flag can change this behavior.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `server`, `location`      | ✓ Server and Location level           |
| `.htaccess`               | ✓ Supported in .htaccess files          |

**Syntax:**
```nginx
RewriteCond input_string pattern [flags]
```

**Parameters:**

- **input_string** - Variable or string to test (e.g., `%REQUEST_URI`, `%HTTP_HOST`, `%REQUEST_FILENAME`)
  - Common variables: `REQUEST_URI`, `HTTP_HOST`, `REQUEST_METHOD`, `REQUEST_FILENAME`

- **pattern** - Test pattern for the condition:
  - **File tests:** `-f` (file exists), `-d` (directory exists), `-x` (executable), `-s` (non-zero size)
  - **Link tests:** `-h` or `-l` (hard/soft link), `-L` (link target exists)
  - **Integer comparisons:** `-lt`, `-le`, `-eq`, `-gt`, `-ge`, `-ne`
  - **String comparisons:** `=`, `>`, `<`, `>=`, `<=`
  - **Regular expressions:** Any regex pattern

- **flags** (optional):
  - `NC` - Case-insensitive matching
  - `OR` / `ornext` - OR logic between conditions (instead of AND)

**Examples:**
```nginx
# Condition: file exists
RewriteCond %REQUEST_FILENAME -f

# Condition: not a directory
RewriteCond %REQUEST_FILENAME !-d

# Condition: string comparison
RewriteCond %{HTTP_HOST} ^www\.example\.com$ [NC]

# Integer comparison (file size > 1024 bytes)
RewriteCond %REQUEST_FILENAME -s
RewriteCond %{FILESIZE} -gt 1024

# Multiple conditions with OR logic
RewriteCond %{HTTP_HOST} ^www\.example\.com$ [OR]
RewriteCond %{HTTPS} off

# Regular expression pattern
RewriteCond %{REQUEST_URI} ^/old/(.*)$
```

---

### 4. RewriteBase

**Purpose:** Set the base URL path for relative substitutions in rewrite rules.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `location`      | ✓ Location level only                  |
| `.htaccess`     | ✓ Supported in .htaccess files          |

**Syntax:**
```nginx
RewriteBase /path/
```

**Parameters:**
- Must start with `/` and end with `/` (trailing slash required)

**Example:**
```nginx
# Inside a specific location
location /blog/ {
    RewriteBase /blog/

    # Relative substitution resolves to: /blog/news.html
    RewriteRule ^news\.html$ news.php [L]
}

# In .htaccess:
RewriteEngine on
RewriteBase /subdir/
RewriteRule ^index\.html$ home.php [L]
```

---

### 5. RewriteOptions

**Purpose:** Configure rewrite behavior options that affect rule inheritance and processing order.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `main`, `server`, `location` | ✓ All three levels supported |

**Syntax:**
```nginx
RewriteOptions option1 [option2 ...]
```

**Possible Options:**

| Option          | Description |
|-----------------|-------------|
| `Inherit`       | Inherit rules from parent locations/servers (default behavior) |
| `InheritBefore` | Process parent rules before child rules |

**Example:**
```nginx
http {
    server {
        RewriteOptions Inherit

        location /parent/ {
            RewriteRule ^parent/(.*)$ /new/$1 [L]
        }

        location /child/ {
            # Inherits rules from parent by default with 'Inherit' option
            RewriteBase /child/
        }
    }
}
```

---

### 6. RewriteMap

**Purpose:** Define name-value mappings for lookup tables in rewrite expressions.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `server`        | ✓ Server level only                     |
| `.htaccess`     | ✗ Not supported in .htaccess            |

**Syntax:**
```nginx
RewriteMap name type:source
```

**Parameters:**

- **name** - Map identifier (used as variable like `%MAPNAME:value`)
- **type** - Type of map:
  - `int` - Internal function (supported: `tolower`, `toupper`, `escape`, `unescape`)
  - `txt` - Text file lookup (not supported yet)
  - `rnd` - Random lookup (not supported yet)
  - `prg` - Program lookup (not supported yet)

**Example:**
```nginx
server {
    # Map to lowercase version of hostname
    RewriteMap lc int:tolower

    server_name example.com;

    location / {
        # Use mapped value in rule
        RewriteRule ^lc/([^/]+)$ /redirect/$1 [L]
    }
}

# Usage in rules:
RewriteRule ^(.*)$ /%lc:$1 [L]
```

---

### 7. HtaccessEnable

**Purpose:** Enable or disable `.htaccess` file parsing for the server/location.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `main`, `server`      | ✓ Main and Server level only          |
| `.htaccess`     | ✗ Not supported in .htaccess            |

**Syntax:**
```nginx
HtaccessEnable on|off
```

**Parameters:**
- `on`  - Enable `.htaccess` parsing
- `off` - Disable `.htaccess` parsing (default)

**Example:**
```nginx
http {
    server {
        HtaccessEnable on

        # .htaccess files will be searched upward from the request path
        location /subdir/ {
            root /var/www/html;
        }
    }
}

# In nginx.conf, you can also enable at http level:
http {
    HtaccessEnable on;

    server {
        # Inherits setting from http level
    }
}
```

---

### 8. HtaccessName

**Purpose:** Specify an alternative filename for `.htaccess` files.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `main`, `server`      | ✓ Main and Server level only          |
| `.htaccess`     | ✗ Not supported in .htaccess            |

**Syntax:**
```nginx
HtaccessName filename
```

**Parameters:**
- Any string (default is `.htaccess`)

**Example:**
```nginx
server {
    HtaccessName .webconfig

    # Will look for .webconfig files instead of .htaccess
    location / {
        root /var/www/html;
    }
}

# In .htaccess:
HtaccessEnable on
HtaccessName .custom_htac
```

---

### 9. RewriteFallBack

**Purpose:** Specify an alternative fallback path when the rewritten file doesn't exist (instead of default `/index.php`). This directive can only be used in `.htaccess` files and is read by `ngx_htaccess_parse_file_from_ha()`. The fallback path is cached per request and retrieved in `ngx_http_apache_rewrite_url_register_hook_with_fallback()`.

| Context        | Available Levels                            |
|----------------|---------------------------------------------|
| `.htaccess`     | ✓ Supported only in .htaccess files      |

**Syntax:**
```apache
RewriteFallBack /path/to/fallback.php
```

**Parameters:**
- Path must start with `/` (required)
- Default fallback is `/index.php` if not specified

**Example in .htaccess:**
```apache
# Use custom fallback when file not found
RewriteFallBack /custom/app.php

# Fallback will redirect to /custom/app.php?query_string instead of /index.php
RewriteEngine on
RewriteCond %{REQUEST_FILENAME} !-f
RewriteRule ^(.*)$ index.php [QSA,L]
```

**Example with query string:**
```apache
RewriteFallBack /handler.php?lang=ru

# When file doesn't exist, request is redirected to the fallback path
# preserving the original query string if present
```

---

## Configuration Levels Summary

| Directive       | http/main | server | location | .htaccess |
|-----------------|-----------|--------|----------|-----------|
| RewriteEngine   | ✓         | ✓      | ✓        | ✓         |
| RewriteRule     | -         | ✓      | ✓        | ✓         |
| RewriteCond     | -         | ✓      | ✓        | ✓         |
| RewriteBase     | -         | -      | ✓        | ✓         |
| RewriteOptions  | ✓         | ✓      | ✓        | -         |
| RewriteMap      | -         | ✓      | -        | -         |
| HtaccessEnable  | ✓         | ✓      | -        | -         |
| HtaccessName    | ✓         | ✓      | -        | -         |
| RewriteFallBack | ✗         | ✗      | -        | ✓         |

---

## .htaccess File Format

`.htaccess` files follow the Apache format:

```apache
# Comments start with #
RewriteEngine on

RewriteCond %{REQUEST_URI} ^/old/(.*)$
RewriteRule ^old/(.*)$ /new/$1 [R=301,L]

# Multiple conditions (AND logic by default)
RewriteCond %{HTTP_HOST} www\.example\.com$
RewriteCond %{HTTPS} off
RewriteRule (.*) https://www.example.com/$1 [R=301,L]

RewriteBase /subdir/
RewriteFallBack /custom/fallback.php
RewriteCond !-f
RewriteCond !-d
RewriteRule ^(.*)$ index.php?route=$1 [QSA,L]
```

When a file doesn't exist after URL rewriting, the module falls back to:
1. `RewriteFallBack` path if specified in `.htaccess`, or
2. `/index.php` as default fallback

---

## Module Features

### FastCGI Integration
The module automatically passes environment variables (set via `[E=VAR:VAL]` flags) to FastCGI applications without manual configuration.

### Environment Variables
Environment variables persist across request phases and are available for downstream modules.

### .htaccess Caching
`.htaccess` files are cached by modification time in the request pool to improve performance on repeated requests. The `RewriteFallBack` directive is also stored in the cache entry and retrieved when needed.

### RewriteFallBack Directive
The `RewriteFallBack` directive allows customizing the fallback path used when a rewritten file doesn't exist:

1. **Caching:** The fallback path from `.htaccess` is cached per request to avoid repeated parsing
2. **Fallback Logic:** When `try_files` fails, the module redirects to the configured fallback instead of `/index.php`
3. **Query String Preservation:** Original query string is preserved and appended to fallback path

---

## Notes

- Rewrite rules are processed in order as defined in configuration or `.htaccess` files
- The `[L]` flag stops processing at the current rule
- The `[N]` flag enables looping for infinite redirects prevention (max 10,000 rounds)
- Location-level rules override server-level rules unless `Inherit` option is set
