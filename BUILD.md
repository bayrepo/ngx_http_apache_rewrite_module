# Package Build

## Building a binary file using a Docker image

To build the image, download and extract nginx to the project root:
For example:
```
wget https://nginx.org/download/nginx-1.26.3.tar.gz
tar xvf nginx-1.26.3.tar.gz
```
Next, build the image using the Dockerfile in the project root (build for almalinux:9):
```
docker build -t nginx-mod_rewrite .
```

We obtain an image with nginx and the built mod_rewrite.

To run the image, you need to execute the command:
```
docker run --name nginx-mod_rewrite -p 8080:80 -p 8081:8081 nginx-mod_rewrite
```

## Building the package for a specified operating system

In the project root there is a script `package_preparer.sh` that allows you to build an rpm or deb package for a specific operating system. Which OS and native nginx version the package will be built for depends on the image used.

To build the package, run the command:
```
bash package_preparer.sh prepare "almalinux:9"
```

The build was tested on the following images:

* almalinux:9
* almalinux:8
* rockylinux:9
* ubuntu:24.04
* debian:stable

After the build, the packages will appear in the `tmpbuild` directory:
```
$ ls -1 tmpbuild/*.rpm
tmpbuild/nginx-mod-rewrite-0.1-1.el9.src.rpm
tmpbuild/nginx-mod-rewrite-0.1-1.el9.x86_64.rpm
tmpbuild/nginx-mod-rewrite-debuginfo-0.1-1.el9.x86_64.rpm
tmpbuild/nginx-mod-rewrite-debugsource-0.1-1.el9.x86_64.rpm
```
or
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

To ensure that the command
```bash
bash -x package_preparer.sh prepare "os-image"
```
works correctly, the following utilities must be installed and available on the system (each of which comes from a package supplied by your Linux distribution):

| Utility | What it does in the `prepare` section | Package (Debian/Ubuntu) |
|---------|---------------------------------------|--------------------------|
| **`bash`** | Runs the script itself | already installed |
| **`rm`, `mkdir`, `cp`, `find`, `head`, `gzip`, `tee`** | Cleans/creates directories, copies files, searches, compresses, logs | `coreutils` |
| **`sed`** (GNU sed) | Parameterizes templates, removes/replaces strings | `sed` (GNU sed, the `sed` package from coreutils) |
| **`awk`** | Extracts the version from `CHANGELOG`, generates the changelog file | `gawk` (or the `awk` package) |
| **`tar`** | Creates tar archives, adds files | `tar` |
| **`docker`** (commands `docker build`, `docker run`, `docker rmi`) | Builds a temporary image, runs a container, cleans up | `docker.io` / `docker-ce` + running **daemon** |

> **Note on `sed`**  
> The script uses extended regular expressions (`sed -E`) and in-place file modification (`sed -i`). This is the GNU variant of `sed`. On systems where `sed` is the BSD variant (e.g., macOS), the command `sed -E -i` will not work, so in such environments you need to install GNU `sed` (`gsed`) and replace calls with `gsed`.

The Docker daemon must be running, and the user must have permission to use it (or be in the `docker` group).

The main build occurs in a Docker container, so the base system does not get cluttered with extra packages. The set of utilities used by the script is present in the distribution by default, so you only need to install Docker.

The builder analyzes the nginx version installed in the distribution and performs the build in an environment with source files of the same nginx version, using commands similar to those used when building nginx from the distribution.

## Manual build from sources

If you need to build the module yourself, you need to run the following commands.

Here is an example for AlmaLinux 9.

Install the necessary build packages:
```
dnf -y install openssl-devel pcre-devel zlib-devel gcc gcc-c++ make wget
```

In the project root:
```
bash package_preparer.sh download 1.22.1
```

You can specify any available version on the website `https://nginx.org/ru/download.html`; it will be downloaded and extracted into the current directory.

Then change directory into nginx-1.22.1:
```
cd nginx-1.22.1
```
and set the build configuration. Here is an example configuration, it may differ; this example is a typical configuration:
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

During execution, the following should appear:
```
adding module in ../modules/mod_rewrite
 + ngx_http_apache_rewrite_module was configured
```

You can add `--add-dynamic-module=../modules/mod_rewrite` to your nginx configuration.

Next:
```
make modules
```
This command will build the module:
```
# ls -1 objs/ngx_http_apache_rewrite_module.so
objs/ngx_http_apache_rewrite_module.so
```
Then you can move it to the nginx modules directory:
```
cp objs/ngx_http_apache_rewrite_module.so /etc/nginx/modules/
```
and load the module in the nginx configuration file:
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
```
