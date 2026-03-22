# Тесты Apache mod_rewrite для nginx - ПОЛНАЯ СУММАРИЯ

## 📁 Структура тестовых файлов

```
/test1/test/
├── .htaccess              # Главное правило rewrite для корневого каталога
├── index.html             # Главная страница (назначение для redirect)
├── show.html              # Показывается вместо redirect.html
├── stop.html              # Блокируется (403 Forbidden)
├── redirect.html          # Внутреннее перенаправление на show.html
├── test.xmx               # Файл .xmx для тестирования расширения
├── README.md              # Документация по тестам
├── SUMMARY.md             # Эта страница - полная суммари
└── subdir/                # Подкаталог с дополнительными тестами
    ├── .htaccess          # Правила rewrite для подкаталога
    ├── file.html          # Файл в подкаталоге
    └── other.xmx          # Другой файл .xmx (блокируется)
```

---

## 🔥 Ключевые правила .htaccess

### 1️⃣ Перенаделение файлов с расширением `.xmx` на index.html

**Правило:**
```apache
RewriteRule \.xmx$ /index.html [R=301,L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/test.xmx` | 301 Redirect → `/test/index.html` |
| URL в браузере меняется на `/test/index.html` | ✅ Внешнее перенаправление |

**Логика:** 
- Все файлы заканчивающиеся на `.xmx` перенаправляются на главную страницу
- Используется флаг `[R=301]` для внешнего HTTP redirect
- Флаг `[L]` останавливает дальнейшую обработку правил

---

### 2️⃣ Блокировка доступа к stop.html (403 Forbidden)

**Правила:**
```apache
RewriteCond %{REQUEST_FILENAME} =stop.html
RewriteRule ^ - [F,L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/stop.html` | 403 Forbidden (Доступ запрещен) |
| Страница не отображается | ✅ Полная блокировка |

**Логика:**
- `RewriteCond` проверяет что запрос именно к stop.html
- `RewriteRule ^ - [F]` возвращает ошибку 403 Forbidden
- Флаг `[L]` останавливает обработку

---

### 3️⃣ Внутреннее перенаправление redirect.html на show.html

**Правило:**
```apache
RewriteRule ^redirect\.html$ show.html [L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/redirect.html` | Показывается content из show.html |
| URL в браузере остается `/test/redirect.html` | ✅ Internal rewrite |

**Логика:**
- Правило скрывает redirect.html и показывает вместо него show.html
- Происходит внутренняя переадресация без изменения URL
- Флаг `[L]` останавливает обработку

---

### 4️⃣ Перенаделение подкаталога subdir на index.html

**Правило:**
```apache
RewriteRule ^subdir/?$ /index.html [L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/subdir/` | Показывается content из index.html |
| URL в браузере остается `/test/subdir/` | ✅ Internal rewrite |

---

### 5️⃣ Дополнительные проверки (из main .htaccess)

**Правило:**
```apache
RewriteCond %{REQUEST_FILENAME} !test\.xmx$
RewriteRule \.xmx$ - [F,L]
```

**Ожидаемое поведение:**
- Блокирует все `.xmx` файлы кроме test.xmx
- **НО:** Работает ПОСЛЕ правила 1, поэтому test.xmx сначала редиректится на index.html

---

### 6️⃣ Перенаделение subdir с последующей блокировкой (конфликт)

**Порядок в .htaccess:**
```apache
# Сначала перенаправление
RewriteRule ^subdir/?$ /index.html [L]

# Затем блокировка
RewriteRule ^subdir/?$ - [F,L]
```

**Ожидаемое поведение:**
- subdir сначала переадресуется на index.html (правило 4)
- Затем применяется правило блокировки → 403 Forbidden
- **Итог:** subdir заблокирован с ошибкой 403

---

## 📁 Правила в подкаталоге `subdir/.htaccess`

### 1️⃣ Блокировка всех .xmx файлов в subdir (кроме test.xmx)

**Правило:**
```apache
RewriteRule \.xmx$ - [F,L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/subdir/other.xmx` | 403 Forbidden |
| Файл заблокирован | ✅ |

---

### 2️⃣ Внутреннее перенаправление subdir/file.html на ../show.html

**Правило:**
```apache
RewriteRule ^file\.html$ ../show.html [L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/subdir/file.html` | Показывается content из show.html (в родительском каталоге) |
| URL остается `/test/subdir/file.html` | ✅ Internal rewrite с относительным путем |

---

### 3️⃣ Блокировка всех файлов кроме .html в subdir

**Правило:**
```apache
RewriteCond %{REQUEST_FILENAME} !-ext=html
RewriteRule ^ - [F,L]
```

**Ожидаемое поведение:**
| Запрос | Результат |
|--------|-----------|
| `http://localhost/test/subdir/file.txt` | 403 Forbidden |
| Только .html файлы доступны | ✅ Фильтрация по расширению |

---

## 🎯 Тестирование функционала mod_rewrite

### Поддерживаемые флаги:

| Флаг | Значение | Описание |
|------|----------|----------|
| `L` | Last | Остановить обработку остальных правил в .htaccess |
| `F` | Forbidden | Возвращать ошибку 403 Forbidden |
| `R=301` | Redirect | Внешнее перенаправление с кодом HTTP 301 Moved Permanently |
| `END` | End | Остановить ВСЮ обработку rewrite (включая nginx config rules) |

### Поддерживаемые директивы:

| Директива | Описание |
|-----------|----------|
| `RewriteEngine on/off` | Включение/выключение mod_rewrite для .htaccess |
| `RewriteRule pattern substitution [flags]` | Правило перенаправления |
| `RewriteCond test string` | Условие для правила RewriteRule |

---

## 📊 Механизм приоритета

### Порядок применения правил:

```
┌───────────────────────────────┐
│ 1. Правила из .htaccess       │ ← Higher Priority
│    ├─ Read & parse            │
│    ├─ Convert to mod_rewrite  │
│    └─ Apply rules             │
│                                │
├───────────────────────────────┤
│ Check END flag                │
│ if (ctx->end) STOP ALL        │ ← Blocks nginx config!
├───────────────────────────────┤
│ 2. Правила из nginx config    │ ← Lower Priority
│    └─ Apply from server/loc   │
└───────────────────────────────┘
```

**Ключевой момент:**
- При наличии флага `[END]` в .htaccess, обработка ВСЕХ правил rewrite останавливается
- Это позволяет полностью блокировать nginx config rules при необходимости

---

## 🧪 Примеры CURL запросов для тестирования

```bash
# 1. Перенаправление .xmx на index.html (301)
curl -I http://localhost/test/test.xmx
# Ожидаемый: HTTP/1.1 301 Moved Permanently
#            Location: /test/index.html

# 2. Блокировка stop.html (403)
curl -I http://localhost/test/stop.html
# Ожидаемый: HTTP/1.1 403 Forbidden

# 3. Internal rewrite redirect.html → show.html (200 OK, content из show.html)
curl -s http://localhost/test/redirect.html | grep h1
# Ожидаемый: <h1>show.html</h1>

# 4. Internal rewrite subdir → index.html (200 OK)
curl -I http://localhost/test/subdir/
# Ожидаемый: HTTP/1.1 200 OK

# 5. Блокировка other.xmx в subdir (403)
curl -I http://localhost/test/subdir/other.xmx
# Ожидаемый: HTTP/1.1 403 Forbidden

# 6. Internal rewrite subdir/file.html → ../show.html (200 OK)
curl -s http://localhost/test/subdir/file.html | grep h1
# Ожидаемый: <h1>show.html</h1>
```

---

## 📝 Установка и запуск тестов

### Конфигурация nginx для запуска тестов:

```nginx
server {
    listen 80;
    server_name localhost;
    
    # Корневой сайт
    location / {
        root /workspaces/test1/nginx-1.25.3/html;
    }
    
    # Тестовый каталог с .htaccess поддержкой
    location /test {
        alias /workspaces/test1/test/;
        
        HtAccess on;          # Включить чтение .htaccess файлов
        HtAccessFile .htaccess;  # Файл конфигурации
        
        # Для подкаталога subdir (авто-поиск .htaccess)
    }
}
```

### Команды запуска:

```bash
# 1. Скомпилировать nginx с модулем mod_rewrite
cd /workspaces/test1/nginx-1.25.3 && make modules

# 2. Запустить nginx
./nginx -c /path/to/nginx.conf

# 3. Тестировать правила через curl или браузер
curl http://localhost/test/redirect.html
```

---

## ✅ Итог

Модуль `ngx_http_apache_rewrite_module` обеспечивает:

1. ✅ **Чтение .htaccess файлов** при каждом запросе с проверкой mtime
2. ✅ **Кэширование директив** в памяти на время жизненного цикла запроса  
3. ✅ **Конвертацию Apache mod_rewrite правил** в формат nginx mod_rewrite
4. ✅ **Приоритет .htaccess выше nginx config rules**
5. ✅ **Флаг [END] для полной остановки обработки rewrite**
6. ✅ **Поддержка флагоv L, F, R=301, END**

Все тестовые файлы готовы и содержат документацию по ожидаемому поведению! 🎉