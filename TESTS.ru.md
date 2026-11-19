# Интеграционные тесты

## Обзор

Интеграционные тесты проверяют поведение модуля `.htaccess` mod_rewrite на реальных конфигурациях CMS. Каждый набор тестов выполняется внутри Docker-контейнера с nginx + php-fpm и проверяет HTTP-ответы (коды статусов, содержимое тела ответа, заголовки перенаправления) через curl.

Тесты фиксируют **текущее поведение модуля как baseline**. Известные ограничения документированы непосредственно в тестовых скриптах комментариями `NOTE:`. При исправлении бага модуля следует обновить соответствующее ожидание в тесте.

## Требования

- Docker Engine (20.10+)
- Docker Compose v2 (`docker compose`)
- Доступ в интернет (для загрузки базового образа `almalinux:9` и исходников nginx при первой сборке)

## Быстрый старт

```bash
cd tests
docker compose up --build --abort-on-container-exit
```

Эта команда:
1. Соберёт модуль из локальных исходников (`modules/mod_rewrite/`) против системного nginx из AlmaLinux 9
2. Запустит nginx + php-fpm с тестовыми сайтами
3. Последовательно прогонит все наборы тестов
4. Выведет результаты и завершится с кодом 0 (все пройдены) или 1 (есть падения)

### Повторный запуск без пересборки

Если изменились только тестовые скрипты (не исходники модуля):

```bash
docker compose up --abort-on-container-exit
```

### Очистка

```bash
docker compose down -v
```

### Запуск против Apache + mod_rewrite (эталон)

Те же наборы тестов можно запустить против Apache httpd + mod_rewrite для проверки ожидаемого поведения:

```bash
cd tests
docker compose -f docker-compose.apache.yml up --build --abort-on-container-exit
```

Падения в режиме Apache означают:
- **Ожидания теста специфичны для nginx** (задокументированы комментариями `NOTE:`) — показывают где модуль расходится с Apache
- **Проблема конфигурации Apache** (ошибки 500) — требуется настройка httpd.conf
- **Реальные баги модуля** — когда Apache проходит, а nginx-модуль падает на том же `.htaccess`

### Очистка Apache

```bash
docker compose -f docker-compose.apache.yml down -v
```

## Тестовая среда

| Компонент | Детали |
|-----------|--------|
| Базовая ОС | AlmaLinux 9 |
| nginx | Системный пакет (на данный момент 1.20.1) |
| PHP | Системные php-fpm + php-cli |
| Сборка модуля | `--with-compat --add-dynamic-module` |
| Архитектура | x86_64 |

### Архитектура контейнеров

Два контейнера под управлением docker compose:

- **build** — компилирует `ngx_http_apache_rewrite_module.so` из смонтированных исходников, результат кладёт в shared volume
- **test** — запускает nginx + php-fpm, монтирует собранный `.so`, файлы сайтов из `cms/` и тестовые скрипты

Исходные файлы (`modules/`, `cms/`) монтируются в режиме read-only — изменения подхватываются при следующем запуске без пересборки образа.

### Конфигурация сайтов

Каждая CMS работает на отдельном порту:

| Порт | CMS | Корневой путь | PHP |
|------|-----|---------------|-----|
| 8081 | Simple | `/sites/simple` | Нет |
| 8082 | Drupal | `/sites/drupal` | Да |
| 8083 | WordPress | `/sites/wordpress/wordpress` | Да |
| 8084 | Joomla | `/sites/joomla` | Да |
| 8085 | Grav | `/sites/grav` | Да |
| 8086 | WordPress Multisite | `/sites/wordpress-multy` | Да |

На всех сайтах включены `HtaccessEnable on`, `RewriteEngine On`. На PHP-сайтах дополнительно включены `FallbackToIndex on` и настроен FastCGI.

## Наборы тестов

### test-simple.sh (7 тестов)
Базовая функциональность mod_rewrite без PHP:
- Внешнее перенаправление (`.xmx` → `/index.html`, 301)
- Правило запрета (`stop.html`, 403)
- Внутренняя перезапись (`redirect.html` → `show.html`, 200)
- Правила для подкаталогов
- Вложенный `.htaccess` в `subdir/` (блокировка `.xmx`, перезапись `file.html`)
- Доступ к существующим файлам

### test-drupal.sh (10 тестов)
Правила `.htaccess` Drupal:
- Защита скрытых файлов (`.htaccess`, `.htpasswd` → 403)
- Исключение `.well-known` (известное ограничение: negative lookahead не поддерживается → 403)
- Перенаправление `install.php` / `rebuild.php` (известное ограничение: backreferences `%1/%2` не поддерживаются → маршрутизация через index.php)
- Доступ к `favicon.ico` (известное ограничение: dot-pattern regex блокирует)
- Исключения для тестовых файлов ядра (`https.php`, `http.php` → 200)
- Проброс заголовка Authorization

### test-wordpress.sh (8 тестов)
Правила `.htaccess` WordPress:
- Проброс `[E=HTTP_AUTHORIZATION]` в PHP (REST API mock проверяет токен)
- Неавторизованный запрос → 401
- Маршрутизация с `RewriteBase /wordpress`
- Прямой доступ к `index.php` (без перезаписи)
- Условия `!-f` / `!-d` для несуществующих путей
- Доступ к существующим файлам (`existing.jpg`)
- Каталог без индексного файла → 403

### test-joomla.sh (9 тестов)
Правила `.htaccess` Joomla:
- Правила безопасности через `%{QUERY_STRING}` (известное ограничение: переменная не реализована → запросы проходят)
- Проброс заголовка Authorization в API
- Маршрутизация `/api/` на `api/index.php`
- Проверка авторизации при прямом доступе к `api/index.php` (401 без токена)
- Основная маршрутизация несуществующих страниц
- SEF URL `/index.php/component/config`

### test-grav.sh (18 тестов)
Правила `.htaccess` Grav CMS (наиболее полный набор):
- Паттерны безопасности через `%{QUERY_STRING}` (известное ограничение)
- Основная маршрутизация страниц через `index.php`
- Доступ к существующим файлам/каталогам (известное ограничение: dot-pattern блокирует некоторые)
- Блокировка чувствительных каталогов (`cache/`, `backup/`, `system/`, `vendor/`)
- Защита файловых расширений (`.md` → 403)
- Защита скрытых файлов (`.htpasswd` → 403)
- Блокировка критических конфигурационных файлов (`LICENSE.txt`, `composer.lock`, `composer.json`, `.htaccess`)

### test-wp-multisite.sh (13 тестов)
Правила `.htaccess` WordPress Multisite:
- Обработка заголовка Authorization
- Маршрутизация корня с `RewriteBase /`
- Прямой доступ к `index.php`
- Перенаправление `wp-admin` с добавлением слеша (301)
- Доступ к статическим файлам (`style.css`, `script.js`)
- Маршрутизация путей ядра WordPress (`wp-admin/`, `wp-content/`, `wp-includes/`)
- Прямой доступ к пользовательским PHP-файлам
- Маршрутизация несуществующих страниц/каталогов через `index.php`

## Формат вывода тестов

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

Код возврата: `0` = все пройдены, `1` = есть падения.

## Известные ограничения модуля (задокументированы в тестах)

| Ограничение | Затронутые наборы | Влияние |
|-------------|-------------------|---------|
| `%{QUERY_STRING}` regex с экранированными символами | Joomla, Grav | Правила безопасности с `\(` `\)` в QUERY_STRING условиях могут не срабатывать |
| Backreferences `%1`/`%2` в подстановке RewriteRule | Drupal | Сложные условные перенаправления проходят сквозь правила на index.php |
| Целочисленные сравнения `-lt`/`-eq`/`-gt` | Тесты директив | Apache использует другой порядок аргументов для целочисленных сравнений в .htaccess |

## Apache vs Nginx модуль: ожидаемые различия в тестах

При запуске тех же наборов тестов против Apache + mod_rewrite (`docker compose -f docker-compose.apache.yml`), некоторые тесты дают разные результаты. Таблица ниже документирует ожидаемые различия:

### Тесты которые проходят на Apache, но не на nginx-модуле

Все ранее выявленные различия исправлены. Модуль теперь ведёт себя идентично Apache mod_rewrite для всех тестируемых сценариев. Оставшиеся различия носят чисто архитектурный характер (см. ниже).

### Тесты с различиями из-за архитектурных особенностей (не исправимо)

| Тест | Результат Apache | Результат Nginx | Причина |
|------|-----------------|-----------------|---------|
| Переменная `SERVER_SOFTWARE` | Содержит "Apache" | Содержит "nginx" | Разная идентификация сервера |
| Директива `RewriteFallBack` | 500 (директива неизвестна) | 200 (кастомная директива) | Не является директивой Apache; расширение nginx-модуля |
| Каталог без индексного файла | 403 (Forbidden) | 403 или 404 | Различное поведение по умолчанию при autoindex off |
| `[E=VAR:VAL]` non-HTTP_ переменные в PHP `$_SERVER` | Доступны через `getenv()` | Не доступны | Apache mod_php работает in-process; nginx php-fpm — отдельный процесс |

### Тесты которые теперь проходят идентично на обоих серверах

| Функциональность | Статус |
|------------------|--------|
| Negative lookahead `(?!...)` в regex | ✅ Исправлено — парсер сохраняет backslashes корректно |
| Все флаги редиректа `[R=301]`, `[R=302]`, `[R=307]`, `[R=permanent]` и т.д. | ✅ Идентично |
| `[G]` Gone, `[F]` Forbidden, `[L]` Last, `[END]` | ✅ Идентично |
| `[NC]` case-insensitive, `[NE]` no-escape | ✅ Идентично |
| `[S=N]` Skip, `[C]` Chain, `[QSA]`, `[QSD]` | ✅ Идентично |
| `RewriteBase`, `RewriteMap` (tolower/toupper/escape/unescape) | ✅ Идентично |
| Переменные: `REQUEST_URI`, `HTTP_HOST`, `DOCUMENT_ROOT`, `HTTPS`, `TIME_*`, `SERVER_*`, `REMOTE_*` | ✅ Идентично |
| Файловые условия: `-f`, `-d`, `-s`, `-x` | ✅ Идентично |
| Строковые/целочисленные сравнения, отрицание паттерна | ✅ Идентично |
| `HtaccessName`, `GlobalLocationRewriteEngine`, `InheritBefore` | ✅ Идентично |

## Добавление новых тестов

Тестовый раннер автоматически обнаруживает все файлы `tests/scripts/suites/test-*.sh` — регистрация не требуется.

### Доступные функции проверки

| Функция | Аргументы | Назначение |
|---------|-----------|------------|
| `test_status` | описание, url, ожидаемый_код | Проверка только HTTP-статуса |
| `test_content` | описание, url, ожидаемый_код, подстрока, [заголовки] | Статус + наличие подстроки в теле ответа |
| `test_redirect` | описание, url, ожидаемый_код, ожидаемый_location | Статус + совпадение заголовка Location |

### Добавление теста на существующий порт

Используйте этот вариант, когда тест подходит для существующего сайта (например, добавление нового правила в сайт WordPress или Simple).

1. Добавьте тестовые данные в соответствующую директорию `cms/<сайт>/`:
   - Правила `.htaccess`
   - HTML/PHP файлы ответов с уникальными маркерами содержимого

2. Добавьте проверки в существующий тестовый скрипт `tests/scripts/suites/test-<сайт>.sh`:
   ```bash
   # Пример: добавить в test-wordpress.sh
   test_content "Новое правило маршрутизирует /custom/ на custom.php" \
       "$BASE/custom/" "200" "Custom Content Marker"
   ```

3. Запустите тесты для проверки:
   ```bash
   cd tests && docker compose up --build --abort-on-container-exit
   ```

### Добавление теста на новый порт

Используйте этот вариант, когда нужен изолированный сайт со своим серверным блоком nginx (например, для тестирования директив, требующих конфигурации на уровне server: `RewriteMap`, `HtaccessName`, `GlobalLocationRewriteEngine`).

#### Шаг 1: Создайте тестовые данные

```bash
mkdir -p cms/test-my-feature/
```

Создайте `.htaccess` с вашими правилами:
```apache
# cms/test-my-feature/.htaccess
RewriteEngine On
RewriteBase /test-my-feature/
RewriteRule ^my-test$ result.html [L]
```

Создайте файлы ответов с уникальными маркерами содержимого:
```html
<!-- cms/test-my-feature/result.html -->
<h1>my-feature-result</h1>
```

Для PHP-тестов создайте `.php` файлы, выводящие JSON:
```php
<?php
header("Content-Type: application/json");
echo json_encode(["key" => $_SERVER["SOME_VAR"] ?? "(not set)"]);
```

#### Шаг 2: Добавьте серверный блок nginx

Отредактируйте `tests/nginx/nginx.conf` и добавьте новый серверный блок **перед закрывающей `}` блока `http`**:

```nginx
# ============================================================
# My Feature Test — port 80XX
# ============================================================
server {
    listen 80XX;
    server_name localhost;

    root /sites;                          # общий root для всех сайтов
    HtaccessEnable on;
    RewriteEngine On;
    FallbackToIndex off;                  # включите/выключите по необходимости

    # Директивы уровня server при необходимости:
    # RewriteMap mymap int:tolower;
    # HtaccessName .custom;
    # GlobalLocationRewriteEngine on;

    location / {
        RewriteEngine On;
    }

    # Добавьте PHP location block для PHP-тестов:
    # location ~* \.php$ {
    #     include fastcgi_params;
    #     fastcgi_pass unix:/run/php-fpm/www.sock;
    #     fastcgi_index index.php;
    #     fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
    # }
}
```

> **Важно:** Используйте `root /sites;` (общий root), а не `alias`. Модуль ищет `.htaccess` относительно `root`, и `alias` нарушает эту логику. Директория вашего тестового сайта (`cms/test-my-feature/`) доступна по URI-пути `/test-my-feature/...`.

#### Шаг 3: Откройте порт

Отредактируйте `tests/docker-compose.yml` и обновите диапазон портов:
```yaml
ports:
  - "8081-80XX:8081-80XX"
```

#### Шаг 4: Создайте тестовый скрипт

Создайте `tests/scripts/suites/test-my-feature.sh`:
```bash
#!/bin/bash
source "$(dirname "$0")/../lib/test-helpers.sh"

B="http://localhost:80XX"

suite_start "My Feature Test — port 80XX"

test_content "Моё правило работает корректно" \
    "$B/test-my-feature/my-test" "200" "my-feature-result"

# Для PHP-тестов с заголовками:
# test_content "Заголовок Auth передан" \
#     "$B/test-my-feature/auth-check" "200" '"received":true' \
#     "Authorization: Bearer token"

suite_end
```

Сделайте скрипт исполняемым:
```bash
chmod +x tests/scripts/suites/test-my-feature.sh
```

#### Шаг 5: Запустите и итерируйте

```bash
cd tests
docker compose up --build --abort-on-container-exit
```

Если тесты падают, проверьте фактический ответ:
```bash
# Запустите контейнер интерактивно для отладки
docker compose run --rm --entrypoint "sleep 3600" test
# В другом терминале:
docker exec -it <контейнер> bash
# Внутри контейнера:
php-fpm -D && nginx
curl -sv http://localhost:80XX/test-my-feature/my-test
cat /var/log/nginx/error.log | tail -20
```

### Рекомендации

- **Всегда используйте `RewriteBase`** в `.htaccess`, когда сайт находится в поддиректории общего root. Без него паттерны матчатся против полного URI, включающего префикс директории.
- **Используйте уникальные маркеры содержимого** в файлах ответов (например, `<h1>unique-marker</h1>`), чтобы `test_content` мог проверить, что отдан правильный файл.
- **Фиксируйте текущее поведение как baseline.** Если тест выявляет ограничение модуля, документируйте его комментарием `# NOTE:` и устанавливайте ожидаемое значение равным фактическому поведению. Обновляйте ожидание после исправления бага.
- **PHP-тестам нужны оба компонента:** правило перезаписи в `.htaccess` И соответствующий блок `location ~* \.php$` в nginx.conf. Без PHP location block файлы `.php` отдаются как обычный текст.
- **Директивы уровня server** (`RewriteMap`, `HtaccessName`, `HtaccessEnable`, `FallbackToIndex`, `GlobalLocationRewriteEngine`) должны находиться в блоке `server {}`, а не в `location {}`.

## Связь с директорией `cms/`

Директория `cms/` содержит как оригинальные ручные тестовые фикстуры (Drupal, WordPress, Joomla, Grav, Simple, WordPress Multisite), так и новые тестовые сайты для конкретных директив (`test-flag-*`, `test-variable-*`, `test-php-*` и т.д.). Все тестовые данные находятся в `cms/` и монтируются в тестовый контейнер в режиме read-only по пути `/sites/`. Тестовые скрипты в `tests/scripts/suites/` обращаются к этим данным через HTTP-запросы на соответствующий порт и путь.
