# Dockerfile for building nginx with hello module on AlmaLinux 9
# Build stage
FROM almalinux:9 AS builder

# Install build dependencies
RUN dnf -y update && \
    dnf -y install \
    gcc \
    make \
    wget \
    tar \
    openssl-devel \
    pcre-devel \
    zlib-devel \
    git \
    && dnf clean all

# Set working directory
WORKDIR /app

# Copy the entire project into the image
COPY . .

# Build nginx from source with hello module
RUN cd nginx-* && \
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
    --with-pcre-jit && \
    make modules && \
    make install


# Runtime stage
FROM almalinux:9

# Install runtime dependencies
RUN dnf -y update && \
    dnf -y install \
    openssl \
    pcre \
    zlib \
    && dnf clean all

# Copy nginx binary and modules from builder
COPY --from=builder /usr/sbin/nginx /usr/sbin/nginx
COPY --from=builder /etc/nginx /etc/nginx
COPY --from=builder /etc/nginx/modules /etc/nginx/modules
RUN mkdir -p /etc/nginx/modules
RUN mkdir -p /var/lib/nginx/body /var/lib/nginx/proxy /var/lib/nginx/fastcgi /var/lib/nginx/uwsgi /var/lib/nginx/scgi
RUN mkdir -p /tmp/test_nginx_root
RUN echo "helo" > /tmp/test_nginx_root/index.html
COPY --from=builder /var/log/nginx /var/log/nginx
COPY --from=builder /var/run /var/run
RUN mkdir -p /tmp/test_nginx_root2
ADD cms/simple/* /tmp/test_nginx_root2/

# Expose default nginx port
EXPOSE 80
EXPOSE 443
EXPOSE 8081



# Create a non-root user to run nginx
RUN useradd -r -d /var/www nginx && \
    chown -R nginx:nginx /etc/nginx /var/log/nginx /var/run

# Set entrypoint to run nginx
ENTRYPOINT ["/usr/sbin/nginx", "-g", "daemon off;"]
