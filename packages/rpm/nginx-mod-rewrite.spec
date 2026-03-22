%define name nginx-mod-rewrite
%define version 0.1
%define release 1%{?dist}
%define summary "Nginx rewrite module – dynamic module adding mod_rewrite functionality"
%define license Apache-2.0
%define url https://docs.brepo.ru/nginx-mod-rewrite
%define packager "Alexey Berezhok <a@bayrepo.ru>"
%global nginx_moduledir %{_libdir}/nginx/modules
%global nginx_moduleconfdir %{_datadir}/nginx/modules

Name:           %{name}
Version:        %{version}
Release:        %{release}
Summary:        %{summary}
License:        %{license}
URL:            %{url}
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  openssl-devel
BuildRequires:  pcre-devel
BuildRequires:  zlib-devel
BuildRequires:  bash
BuildRequires:  wget
Requires:       nginx

%description
Dynamic Nginx module implementing Apache mod_rewrite functionality.
The package is built from Nginx sources and the module's own files, and then
installs only the compiled shared object into the /etc/nginx/modules directory.

%prep
%setup -q

%build
bash package_preparer.sh download . system
bash package_preparer.sh build

%install
# Install only the dynamic module
mkdir -p %{buildroot}%{nginx_moduledir} %{buildroot}%{nginx_moduleconfdir}
cp *.so %{buildroot}%{nginx_moduledir}
echo 'load_module "%{nginx_moduledir}/ngx_http_apache_rewrite_module.so";' \
    > %{buildroot}%{nginx_moduleconfdir}/ngx_http_apache_rewrite_module.conf

%post
if [ $1 -eq 1 ]; then
    /usr/bin/systemctl reload nginx.service >/dev/null 2>&1 || :
fi


%files
%license LICENSE

%attr(0640,root,root) %{nginx_moduleconfdir}/ngx_http_apache_rewrite_module.conf
%attr(0755,root,root) %{nginx_moduledir}/ngx_http_apache_rewrite_module.so


%changelog
