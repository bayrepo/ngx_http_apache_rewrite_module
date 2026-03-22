# Сборка пакета

## Сборка бинарного файла с использованием docker-образа

Для сборки образа скачайте и разархивируйте nginx в корень проекта:
Например так:
```
wget https://nginx.org/download/nginx-1.26.3.tar.gz
tar xvf nginx-1.26.3.tar.gz
```
Далее соберите образ, используя Dockerfile в корне проекта (сборка под almalinux:9):
```
docker build -t nginx-mod_rewrite .
```

Получаем образ с nginx и собранным mod_rewrite.

Для запуска образа необходимо выполнить команду:
```
docker run --name nginx-mod_rewrite -p 8080:80 -p 8081:8081 nginx-mod_rewrite
```

## Сборка пакета под заданную операционную систему

В корне проекта расположен скрипт `package_preparer.sh`, который позволяет собрать rpm или deb пакет под определенную операционную систему. Под какую операционную систему и нативную версию nginx будет собран пакет, зависит от используемого образа.

Для сборки пакета необходимо запустить команду:
```
bash package_preparer.sh prepare "almalinux:9"
```

Сборка проверялась на образах:

* almalinux:9
* almalinux:8
* rockylinux:9
* ubuntu:24.04
* debian:stable



После сборки собранные пакеты появятся в каталоге tmpbuild:
```
$ ls -1 tmpbuild/*.rpm
tmpbuild/nginx-mod-rewrite-0.1-1.el9.src.rpm
tmpbuild/nginx-mod-rewrite-0.1-1.el9.x86_64.rpm
tmpbuild/nginx-mod-rewrite-debuginfo-0.1-1.el9.x86_64.rpm
tmpbuild/nginx-mod-rewrite-debugsource-0.1-1.el9.x86_64.rpm
```
или
```
$ ls -1 tmpbuild/nginx-mod-rewrite*
tmpbuild/nginx-mod-rewrite_0.1-1_amd64.buildinfo
tmpbuild/nginx-mod-rewrite_0.1-1_amd64.changes
tmpbuild/nginx-mod-rewrite_0.1-1_amd64.deb
tmpbuild/nginx-mod-rewrite_0.1-1.dsc
tmpbuild/nginx-mod-rewrite_0.1-1.tar.gz
tmpbuild/nginx-mod-rewrite-0.1.tar.gz
tmpbuild/nginx-mod-rewrite-dbgsym_0.1-1_amd64.deb
```

Для того чтобы команда
```bash
bash -x package_preparer.sh prepare "os-image"
```
работала корректно, в системе должны быть установлены и доступны следующие утилиты (каждая из которых берётся из пакета, поставляемого в вашем дистрибутиве Linux):

| Утилита | Что делает в `prepare`‑разделе | Пакет (Debian/Ubuntu) |
|---------|--------------------------------|------------------------|
| **`bash`** | Запуск самого скрипта | уже установлен |
| **`rm`, `mkdir`, `cp`, `find`, `head`, `gzip`, `tee`** | Очистка/создание каталогов, копирование файлов, поиск, сжатие, логирование | `coreutils` |
| **`sed`** (GNU sed) | Параметризация шаблонов, удаление/замена строк | `sed` (GNU sed, пакет `sed` из coreutils) |
| **`awk`** | Извлечение версии из `CHANGELOG`, генерация changelog‑файла | `gawk` (или `awk`‑пакет) |
| **`tar`** | Создание tar‑архива, добавление файлов | `tar` |
| **`docker`** (команды `docker build`, `docker run`, `docker rmi`) | Построение временного образа, запуск контейнера, очистка | `docker.io` / `docker-ce` + работающий **daemon** |

> **Замечание по `sed`**  
> В скрипте используются расширенные регулярные выражения (`sed -E`) и модификация файла на месте (`sed -i`). Это GNU‑вариант `sed`. На системах, где `sed` – BSD‑вариант (например, macOS), команда `sed -E -i` не будет работать, поэтому в таких окружениях нужно установить GNU‑`sed` (`gsed`) и заменить вызовы на `gsed`.

Docker‑демон должен быть запущен, и пользователь должен иметь права на его использование (или быть в группе `docker`).

Основная сборка происходит в docker‑контейнере, поэтому основная система не засоряется лишними пакетами. Основной набор утилит, используемых скриптом, присутствует в дистрибутиве по умолчанию, поэтому необходимо только установить docker.

Сборщик анализирует версию nginx, установленную в дистрибутиве, и производит сборку в окружении исходных файлов аналогичной версии nginx дистрибутива, а также командами, аналогичными, которые использовались при сборке nginx из дистрибутива.

## Ручная сборка из исходников

Если же необходимо собрать модуль самостоятельно, необходимо выполнить следующие команды.

Привожу пример для Almalinux 9.

Установите необходимые пакеты для сборки:
```
dnf -y install openssl-devel pcre-devel zlib-devel gcc gcc-c++ make wget
```

В корне проекта:
```
bash package_preparer.sh download 1.22.1
```

В качестве версии можно задать любую доступную на сайте `https://nginx.org/ru/download.html`, она будет скачана и распакована в текущий каталог.

Далее перейти в папку nginx-1.22.1:
```
cd nginx-1.22.1
```
и задать конфигурацию сборки. Я привожу пример конфигурации, она может отличаться; данный пример — это типичная конфигурация:
```
./configure \
    --with-compat \
    --add-dynamic-module=../modules/mod_rewrite \
    --prefix=/etc/nginx \
    --sbin-path=/usr/sbin/nginx \
    --modules-path=/etc/nginx/modules \
    --conf-path=/etc/nginx/nginx.conf \
    --error-log-path=/var/log/nginx/error.log \
    --http-log-path=/var/log/nginx/access.log \
    --pid-path=/var/run/nginx.pid \
    --lock-path=/var/run/nginx.lock \
    --http-client-body-temp-path=/var/lib/nginx/body \
    --http-proxy-temp-path=/var/lib/nginx/proxy \
    --http-fastcgi-temp-path=/var/lib/nginx/fastcgi \
    --http-uwsgi-temp-path=/var/lib/nginx/uwsgi \
    --http-scgi-temp-path=/var/lib/nginx/scgi \
    --with-http_ssl_module \
    --with-http_v2_module \
    --with-http_realip_module \
    --with-http_gzip_static_module \
    --with-http_stub_status_module \
    --with-http_slice_module \
    --with-http_dav_module \
    --with-http_auth_request_module \
    --with-http_secure_link_module \
    --with-stream \
    --with-stream_ssl_module \
    --with-stream_realip_module \
    --with-stream_ssl_preread_module \
    --with-pcre-jit
```

Во время выполнения команды должно появиться:
```
adding module in ../modules/mod_rewrite
 + ngx_http_apache_rewrite_module was configured
```

Можно добавить `--add-dynamic-module=../modules/mod_rewrite` в свою конфигурацию nginx.

Далее:
```
make modules
```
данная команда соберет модуль:
```
# ls -1 objs/ngx_http_apache_rewrite_module.so
objs/ngx_http_apache_rewrite_module.so
```
Далее его можно переложить в каталог модулей nginx:
```
cp objs/ngx_http_apache_rewrite_module.so /etc/nginx/modules/
```
и подключить модуль в файле конфигурации nginx:
```
cat /etc/nginx/nginx.conf

load_module modules/ngx_http_apache_rewrite_module.so;

...

server {
        ...
        HtaccessEnable on;

        RewriteEngine On;

        location / {
         RewriteEngine On;

        }

    }
