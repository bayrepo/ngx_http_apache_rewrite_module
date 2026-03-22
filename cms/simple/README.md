# Тесты Apache mod_rewrite для nginx

## Обзор

Этот каталог содержит тестовые файлы и правила `.htaccess` для проверки функционала модуля `ngx_http_apache_rewrite_module` в nginx, который обеспечивает совместимость с Apache mod_rewrite.

## Файлы

| Файл | Назначение |
|------|------------|
| `index.html` | Главная страница - назначение для перенаправления .xmx файлов |
| `stop.html` | Заблокированная страница (должен возвращать 403) |
| `redirect.html` | Внутреннее перенаправление на show.html (URL не меняется) |
| `show.html` | Показывается вместо redirect.html при внутреннем rewrite |
| `test.xmx` | Тестовый файл для проверки правила перенаправления .xmx расширения |

## Правила .htaccess

### 1. Перенаделение файлов с расширением `.xmx` на index.html

```apache
RewriteRule \.xmx$ /index.html [R=301,L]
```

**Ожидаемое поведение:**
- Запрос `http://localhost/test/test.xmx` → **301 Redirect** к `/test/index.html`
- URL в браузере изменится на `/test/index.html`

### 2. Блокировка доступа к stop.html (403 Forbidden)

```apache
RewriteCond %{REQUEST_FILENAME} =stop.html
RewriteRule ^ - [F,L]
```

**Ожидаемое поведение:**
- Запрос `http://localhost/test/stop.html` → **403 Forbidden** ("Доступ запрещен")
- Страница не будет отображаться

### 3. Внутреннее перенаправление redirect.html на show.html

```apache
RewriteRule ^redirect\.html$ show.html [L]
```

**Ожидаемое поведение:**
- Запрос `http://localhost/test/redirect.html` → **показывается содержимое show.html**
- URL в браузере остается `/test/redirect.html` (не происходит перенаправления)

### 4. Перенаделение подкаталога subdir на index.html

```apache
RewriteRule ^subdir/?$ /index.html [L]
```

**Ожидаемое поведение:**
- Запрос `http://localhost/test/subdir/` → **показывается index.html**
- URL в браузере остается `/test/subdir/`

### 5. Дополнительные проверки

- Флаг `[L]` (Last) - останавливает обработку правил после выполнения
- Флаг `[F]` (Forbidden) - возвращает ошибку 403 Forbidden
- Флаг `[R=301]` - внешнее перенаправление с кодом статуса 301 Moved Permanently

## Порядок применения правил

В модуле nginx приоритет следующий:

1. **Правила из .htaccess** (высокий приоритет)
2. **Правила из nginx конфигурации** (низкий приоритет)

При наличии флага `[END]` в правилах .htaccess, дальнейшая обработка всех правил останавливается.

## Поддержка флагоv mod_rewrite

| Флаг | Значение | Описание |
|------|----------|----------|
| `L` | Last | Остановить обработку остальных правил |
| `F` | Forbidden | Заблокировать доступ (403) |
| `R=301` | Redirect | Внешнее перенаправление с кодом 301 |
| `END` | End | Полностью остановить обработку rewrite |

## Примеры запросов для тестирования

```bash
# Перенаделение .xmx файла на index.html
curl -I http://localhost/test/test.xmx
# Ожидаемый ответ: HTTP/1.1 301 Moved Permanently
# Location: /test/index.html

# Блокировка stop.html
curl -I http://localhost/test/stop.html
# Ожидаемый ответ: HTTP/1.1 403 Forbidden

# Внутреннее перенаправление redirect.html на show.html
curl -I http://localhost/test/redirect.html
# Ожидаемый ответ: HTTP/1.1 200 OK (content из show.html)

# Перенаделение subdir на index.html
curl -I http://localhost/test/subdir/
# Ожидаемый ответ: HTTP/1.1 200 OK (content из index.html)
```

## Установка и запуск

Для запуска тестов потребуется:

1. Скомпилированный nginx с модулем `ngx_http_apache_rewrite_module`
2. Конфигурация nginx, указывающая каталог `test/` как корень сайта
3. Активированный флаг `.htaccess` через директиву `HtAccess on`

Пример конфигурации:

```nginx
server {
    listen 80;
    server_name localhost;
    
    location /test {
        root /workspaces/test1/test;
        
        HtAccess on;
        HtAccessFile .htaccess;
    }
}
```
