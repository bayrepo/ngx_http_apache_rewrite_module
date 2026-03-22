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
}

COMMAND="$1"

case "$COMMAND" in

prepare)

    DOCKER_IMAGE="$2"

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
        -C . LICENSE package_preparer.sh \
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
packageprep)
    PKG_MGR=""
    # Determine package manager and install nginx
    if command -v dnf >/dev/null 2>&1; then
        PKG_MGR="dnf"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget
    elif command -v yum >/dev/null 2>&1; then
        PKG_MGR="yum"
        $PKG_MGR install -y nginx openssl-devel pcre-devel zlib-devel rpm-build gcc gcc-c++ make wget
    elif command -v apt-get >/dev/null 2>&1; then
        PKG_MGR="apt-get"
        apt-get update
        $PKG_MGR install -y nginx debhelper-compat dh-autoreconf libssl-dev libpcre2-dev zlib1g-dev make gcc build-essential wget
    else
        echo "Unsupported package manager."
        exit 1
    fi

    if [ "$PKG_MGR" == "yum" -o "$PKG_MGR" == "dnf" ]; then
        mkdir -p rpmbuild/BUILD rpmbuild/BUILDROOT rpmbuild/RPMS rpmbuild/SOURCES rpmbuild/SPECS rpmbuild/SRPMS
        cp packages/rpm/nginx-mod-rewrite.spec rpmbuild/SPECS
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

    NGINX_VER_OUTPUT=$(nginx -V 2>&1)
    CONFIG_ARGS=$(echo "$NGINX_VER_OUTPUT" | awk -F'configure arguments: ' '{print $2}')
    if [ -z "$CONFIG_ARGS" ]; then
        echo "Could not retrieve nginx configuration arguments."
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

    # Run configure with saved arguments and add mod_rewrite
    read -ra CONFIG_ARRAY <<< "$CONFIG_ARGS"
    ./configure "${CONFIG_ARRAY[@]}" --add-dynamic-module=../modules/mod_rewrite
    make modules
    cp objs/ngx_http_apache_rewrite_module.so ../
;;
*)
    echo "Incorrect command"
    exit 1
;;

esac

exit 0
