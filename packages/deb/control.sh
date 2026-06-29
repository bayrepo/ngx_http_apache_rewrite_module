#!/bin/bash

# This script generates the debian/control file with correct nginx version dependency
# Usage: control.sh [NGINX_VERSION]
# If NGINX_VERSION is not specified, it uses DETECTED_NGINX_VERSION from environment or default "1.24.0"

NGINX_VER="${1:-${DETECTED_NGINX_VERSION:-1.24.0}}"

cat > debian/control << EOF
Source: nginx-mod-rewrite
Section: web
Priority: optional
Maintainer: Alexey Berezhok <a@bayrepo.ru>
Build-Depends: debhelper-compat (=13),
               dh-autoreconf,
               libssl-dev,
               libpcre2-dev,
               zlib1g-dev,
               nginx (=${NGINX_VER})
Standards-Version: 4.7.0
Homepage: https://docs.brepo.ru/nginx-mod_rewrite

Package: nginx-mod-rewrite
Architecture: any
Depends: \${shlibs:Depends}, nginx (=${NGINX_VER})
Provides: nginx-http-module, nginx-apache-rewrite
Description: Nginx rewrite module – dynamic module adding mod_rewrite functionality
 Dynamic Nginx module implementing Apache mod_rewrite functionality.
 The package is built from Nginx sources and the module's own files, and then
 installs only the compiled shared object into /etc/nginx/modules directory.
EOF

echo "Generated debian/control for nginx version: $NGINX_VER"
