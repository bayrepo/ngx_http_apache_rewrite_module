#!/bin/bash

function download_nginx() {

    NGINX_VER="$1"
    CHECK_SYSTEM_NGINX="$2"

    if [ -n "$CHECK_SYSTEM_NGINX" ]; then
        if command -v rpm >/dev/null 2>&1; then
            # rpm – выводим только версию (без релиза)
            PKG_INFO=$(rpm -q nginx --queryformat '%{VERSION}\n' 2>/dev/null)
        elif command -v apt-get >/dev/null 2>&1; then
            # apt – берём «Candidate»‑версию, затем убираем часть после дефиса
            PKG_INFO=$(apt-cache policy nginx | awk '/Candidate:/ {print $2}')
        else
            echo "Unsupported package manager."
            exit 1
        fi

        # Обрезаем до первой части версии (до дефиса)
        if [[ -n "$PKG_INFO" ]]; then
            NGINX_VER=$(echo "$PKG_INFO" | awk -F'-' '{print $1}')
        else
            echo "nginx not installed."
            exit 1
        fi
        if [ -z "$NGINX_VER" ]; then
            echo "Could not determine nginx version from package manager."
            exit 1
        fi
    fi

    if [[ -z "$NGINX_VER" || "$NGINX_VER" == "." ]]; then
        echo "nginx version not specified"
        exit 1
    fi

    if [ ! -e "nginx-${NGINX_VER}" ]; then
        wget "https://nginx.org/download/nginx-${NGINX_VER}.tar.gz" -O "nginx-${NGINX_VER}.tar.gz"
        if [[ $? -ne 0 ]]; then
            echo "nginx version ${NGINX_VER} not found"
            exit 1
        fi
        tar xvf "nginx-${NGINX_VER}.tar.gz"
        if [[ $? -ne 0 ]]; then
            echo "nginx ${NGINX_VER} unpack error"
            exit 1
        fi
    fi
    echo "Built for nginx ${NGINX_VER}" > BUILDINFO.txt
}

COMMAND="$1"

case "$COMMAND" in

prepare)

    DOCKER_IMAGE="$2"
    NGINX_CUSTOM_VER="$3"

    if [ -z "$DOCKER_IMAGE" ]; then
        echo "Set docker image for build"
        exit 1
    fi

    if [ -e tmpbuild ]; then
        rm -rf tmpbuild
    fi
    mkdir tmpbuild

    PAK_VERSION=$(head -n 1 CHANGELOG | sed -E 's/^[[:space:]]*version:[[:space:]]*//I')
    PAK_CHGLOG=$(awk '/^--/{flag=1;next} flag' CHANGELOG)
    # Split PAK_VERSION into version and release
    pkg_ver="${PAK_VERSION%%-*}"
    pkg_rel="${PAK_VERSION##*-}"

    if [ -e "$PAK_VERSION" -o -e "$PAK_CHGLOG" ]; then
        echo "Can/t detect version or change log"
        exit 1
    fi
    tar -cf "tmpbuild/nginx-mod-rewrite-$pkg_ver.tar" \
        -C modules . \
        --transform='s,^,nginx-mod-rewrite-'$pkg_ver'/modules/,'

    tar -rf "tmpbuild/nginx-mod-rewrite-$pkg_ver.tar" \
        -C . LICENSE package_preparer.sh extract_nginx_args.py \
        --transform='s,^,nginx-mod-rewrite-'$pkg_ver'/,'

    gzip -f "tmpbuild/nginx-mod-rewrite-$pkg_ver.tar"

    # Copy the entire packages directory into tmpbuild
    cp -r packages tmpbuild/

    # Path to the spec file
    SPEC_FILE="tmpbuild/packages/rpm/nginx-mod-rewrite.spec"

    if [ -f "$SPEC_FILE" ]; then
        # Replace %define version and %define release lines
        sed -i -E "s/^%define[[:space:]]+version[[:space:]]+.*/%define version ${pkg_ver}/" "$SPEC_FILE"
        sed -i -E "s/^%define[[:space:]]+release[[:space:]]+[0-9]+(.*)$/%define release ${pkg_rel}\1/" "$SPEC_FILE"

        # Append changelog
        cat <<EOF >> "$SPEC_FILE"
$PAK_CHGLOG
EOF
    else
        echo "Spec file $SPEC_FILE not found"
        exit 1
    fi

    # Debian package changelog processing
    DEB_CHANGELOG_FILE="tmpbuild/packages/deb/changelog"
    if [ -f "$DEB_CHANGELOG_FILE" ]; then
        cat > "$DEB_CHANGELOG_FILE" <<EOF
$(awk -v pkg_ver="$pkg_ver" -v pkg_rel="$pkg_rel" '
BEGIN { pkg="nginx-mod-rewrite" }
{
  if ($0 ~ /^\*/) {
    if (prev) {
      print " -- " author " " email "  " date
      print ""
    }
    split($0, a, " ")
    date=a[2]" "a[3]" "a[4]" "a[5]
    author=a[6]" "a[7]
    email=a[8]
    print pkg " (" pkg_ver "-" pkg_rel ") unstable; urgency=medium"
    print ""
    prev=1
  } else if ($0 ~ /^-/) {
    sub(/^- /,"  * ",$0)
    print $0
  }
}
END {
  if (prev) {
    print " -- " author " " email "  " date
    print ""
  }
}
' <<< "$PAK_CHGLOG")
EOF
    else
        echo "Debian changelog file $DEB_CHANGELOG_FILE not found"
        exit 1
    fi
    mkdir -p tmpbuild/packages/deb/debian

    # Move all files and directories from tmpbuild/packages/deb/ except the debian subdirectory
    find tmpbuild/packages/deb -mindepth 1 -maxdepth 1 ! -name debian -exec mv {} tmpbuild/packages/deb/debian/ \;

    cp package_preparer.sh tmpbuild/

    # Copy Dockerfile into tmpbuild and replace placeholder image name
    cp packages/Dockerfile tmpbuild/Dockerfile
    sed -i "s|^FROM image|FROM ${DOCKER_IMAGE}|g" tmpbuild/Dockerfile
    if [ -n "$NGINX_CUSTOM_VER" ]; then
        sed -i "s|^ENTRYPOINT \[ \"bash\", \"package_preparer.sh\", \"packageprep\" \]|ENTRYPOINT [ \"bash\", \"package_preparer.sh\", \"packageprep\", \"$NGINX_CUSTOM_VER\" ]|g" tmpbuild/Dockerfile
    fi

    # Build a temporary Docker image based on the provided base image
    docker build -t tmpbuild_image -f tmpbuild/Dockerfile .

    # Run the temporary container, mount tmpbuild, build the package, and capture logs
    docker run --rm -v "$(pwd)/tmpbuild":/app/tmpbuild tmpbuild_image 2>&1 | tee "$(pwd)/tmpbuild/build.log"

    # Remove the temporary image
    docker rmi -f tmpbuild_image

;;
download)
    download_nginx "$2" "$3"
;;
installdeps)
    # Determine package manager and install nginx
    if command -v dnf >/dev/null 2>&1; then
        PKG_MGR="dnf"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget python3
    elif command -v yum >/dev/null 2>&1; then
        PKG_MGR="yum"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget python3
    elif command -v apt-get >/dev/null 2>&1; then
        PKG_MGR="apt-get"
        apt-get update
        $PKG_MGR install -y nginx debhelper-compat dh-autoreconf libssl-dev libpcre2-dev zlib1g-dev make gcc build-essential wget python3
    else
        echo "Unsupported package manager."
        exit 1
    fi
;;
installmod)
    if command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
        mkdir -p /usr/share/nginx/modules /usr/lib64/nginx/modules/
        cp *.so /usr/lib64/nginx/modules/
        if [ ! -e "/usr/share/nginx/modules/ngx_http_apache_rewrite_module.conf" ]; then
            echo 'load_module "/usr/lib64/nginx/modules/ngx_http_apache_rewrite_module.so";' \
                > /usr/share/nginx/modules/ngx_http_apache_rewrite_module.conf
        fi
    else
        mkdir -p /usr/share/nginx/modules/ /etc/nginx/modules
        cp *.so /usr/share/nginx/modules/
        if [ ! -e "/etc/nginx/modules/ngx_http_apache_rewrite_module.conf" ]; then
            echo 'load_module "/usr/share/nginx/modules/ngx_http_apache_rewrite_module.so";' \
                > /etc/nginx/modules/ngx_http_apache_rewrite_module.conf
        fi
    fi
;;
packageprep)
    PKG_MGR=""
    NGINX_CUSTOM_VER="$2"

    # Determine package manager and install nginx
    if command -v dnf >/dev/null 2>&1; then
        PKG_MGR="dnf"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget python3
    elif command -v yum >/dev/null 2>&1; then
        PKG_MGR="yum"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget python3
    elif command -v apt-get >/dev/null 2>&1; then
        PKG_MGR="apt-get"
        apt-get update
        $PKG_MGR install -y nginx debhelper-compat dh-autoreconf libssl-dev libpcre2-dev zlib1g-dev make gcc build-essential wget python3
    else
        echo "Unsupported package manager."
        exit 1
    fi

    # Auto-detect nginx version if not specified by user
    DETECTED_NGINX_VERSION=""
    if [ -z "$NGINX_CUSTOM_VER" ] || [ "$NGINX_CUSTOM_VER" == "." ]; then
        echo "Auto-detecting nginx version..."

        if command -v rpm >/dev/null 2>&1; then
            # RPM-based systems: detect via package manager and runtime
            PKG_VERSION=$(rpm -q nginx --queryformat '%{VERSION}' 2>/dev/null)
            RUNTIME_VERSION=$(nginx -V 2>&1 | grep 'version:' | head -n 1 | sed -n 's/.*nginx\/\(.*\).*/\1/p')

            if [ -n "$RUNTIME_VERSION" ]; then
                DETECTED_NGINX_VERSION="$RUNTIME_VERSION"
            elif [ -n "$PKG_VERSION" ]; then
                # Fallback to package version (strip release part)
                DETECTED_NGINX_VERSION=$(echo "$PKG_VERSION" | cut -d'-' -f1)
            fi
        elif command -v dpkg >/dev/null 2>&1; then
            # Debian-based systems
            PKG_VERSION=$(dpkg-query -W -f='${Version}\n' nginx 2>/dev/null | head -n 1)
            RUNTIME_VERSION=$(nginx -V 2>&1 | grep 'version:' | head -n 1 | sed -n 's/.*nginx\/\(.*\).*/\1/p')

            if [ -n "$RUNTIME_VERSION" ]; then
                DETECTED_NGINX_VERSION="$RUNTIME_VERSION"
            elif [ -n "$PKG_VERSION" ]; then
                # Handle Debian version format (e.g., "1.24.0-1ubuntu1.1" -> "1.24.0")
                DETECTED_NGINX_VERSION=$(echo "$PKG_VERSION" | cut -d'-' -f1)
            fi
        else
            echo "Error: Unsupported package manager. Unable to auto-detect nginx version."
            exit 1
        fi

        if [ -n "$DETECTED_NGINX_VERSION" ]; then
            echo "Detected nginx version: $DETECTED_NGINX_VERSION"
        else
            echo "Warning: Could not detect nginx version. Using generic download."
        fi
    else
        echo "Using custom nginx version: $NGINX_CUSTOM_VER"
    fi

    if [ "$PKG_MGR" == "yum" -o "$PKG_MGR" == "dnf" ]; then
        mkdir -p rpmbuild/BUILD rpmbuild/BUILDROOT rpmbuild/RPMS rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/SRPMS
        cp packages/rpm/nginx-mod-rewrite.spec rpmbuild/SPECS

        # Determine nginx version to use in spec file
        if [ -n "$NGINX_CUSTOM_VER" ]; then
            NGINX_VER_FOR_SPEC="$NGINX_CUSTOM_VER"
        elif [ -n "$DETECTED_NGINX_VERSION" ]; then
            NGINX_VER_FOR_SPEC="$DETECTED_NGINX_VERSION"
        fi

        # Modify spec file to use detected/custom nginx version
        if [ -n "$NGINX_VER_FOR_SPEC" ]; then
            sed -i "s|%global nginx_version .*|%global nginx_version $NGINX_VER_FOR_SPEC|" rpmbuild/SPECS/nginx-mod-rewrite.spec
        fi

        cp nginx-mod-rewrite-*.tar.gz rpmbuild/SOURCES
        rpmbuild --define='_topdir /app/rpmbuild' -ba rpmbuild/SPECS/nginx-mod-rewrite.spec
        cp rpmbuild/RPMS/x86_64/* /app/tmpbuild
        cp rpmbuild/SRPMS/* /app/tmpbuild
    else

        # Find the tarball file
        TARBALL=$(ls -1 nginx-mod-rewrite-*.tar.gz 2>/dev/null | head -n 1)
        if [ -z "$TARBALL" ]; then
            echo "No tar.gz file found matching pattern."
            exit 1
        fi

        # Copy tarball to packages/deb
        cp "$TARBALL" packages/deb/

        # Store current directory
        CUR_DIR=$(pwd)

        # Change to packages/deb
        cd packages/deb || exit 1

        # Extract, stripping the top-level directory
        tar --strip-components=1 -xzf "$(basename "$TARBALL")"

        # Use custom nginx version if specified, otherwise use auto-detected
        NGINX_VER="${NGINX_CUSTOM_VER:-${DETECTED_NGINX_VERSION:-1.24.0}}"

        # Generate debian/control with correct nginx version
        bash control.sh "$NGINX_VER"

        if [ -n "$NGINX_CUSTOM_VER" ]; then
            sed -i "s|bash package_preparer.sh download . system|bash package_preparer.sh download \"$NGINX_CUSTOM_VER\"|g" rules
        fi

        dpkg-buildpackage -us -uc

        cp ../nginx-mod-rewrite* /app/tmpbuild

        # Return to original directory
        cd "$CUR_DIR" || exit 1
    fi
;;
build)
    # Get nginx configuration arguments
    if ! command -v nginx >/dev/null 2>&1; then
        echo "nginx is not installed."
        exit 1
    fi

    echo "Retrieved configure arguments: $CONFIG_ARGS"

    # Change to nginx source directory
    SRC_DIR=$(ls -d nginx-* 2>/dev/null | head -n 1)
    if [ -z "$SRC_DIR" ]; then
        echo "Nginx source directory not found."
        exit 1
    fi
    cd "$SRC_DIR" || exit 1


    python3 ../extract_nginx_args.py
    make modules
    cp objs/ngx_http_apache_rewrite_module.so ../
;;
*)
    echo "Incorrect command"
    exit 1
;;

esac

exit 0
