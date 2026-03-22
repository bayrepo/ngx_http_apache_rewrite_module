/*
 * Copyright 2026 Alexey Berezhok
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 */

/*
 * ngx_http_apache_rewrite_variable.c
 *
 * Apache mod_rewrite variable lookup for nginx.
 * Maps %{VARNAME} to nginx request fields.
 */

#include "ngx_http_apache_rewrite_engine.h"


/*
 * Lookup an HTTP request header by name (case-insensitive).
 * Converts underscores to dashes for matching.
 */
static ngx_str_t
ngx_rewrite_lookup_header(ngx_http_request_t *r, ngx_str_t *name)
{
    ngx_list_part_t  *part;
    ngx_table_elt_t  *h;
    ngx_uint_t        i;
    u_char            lowered[256];
    ngx_uint_t        len;
    ngx_str_t         empty = ngx_null_string;

    /* Convert header name: underscores to dashes, lowercase */
    len = name->len;
    if (len > sizeof(lowered) - 1) {
        len = sizeof(lowered) - 1;
    }

    for (i = 0; i < len; i++) {
        u_char ch = name->data[i];
        if (ch == '_') {
            ch = '-';
        }
        lowered[i] = ngx_tolower(ch);
    }

    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == len
            && ngx_strncasecmp(h[i].key.data, lowered, len) == 0)
        {
            return h[i].value;
        }
    }

    return empty;
}


/*
 * Build THE_REQUEST string: "METHOD URI PROTOCOL"
 */
static ngx_str_t
ngx_rewrite_build_the_request(ngx_http_request_t *r, ngx_pool_t *pool)
{
    ngx_str_t  result;
    u_char    *p;
    size_t     len;

    len = r->method_name.len + 1 + r->unparsed_uri.len + 1
          + sizeof("HTTP/1.1") - 1;

    p = ngx_pnalloc(pool, len);
    if (p == NULL) {
        ngx_str_null(&result);
        return result;
    }

    result.data = p;
    p = ngx_cpymem(p, r->method_name.data, r->method_name.len);
    *p++ = ' ';
    p = ngx_cpymem(p, r->unparsed_uri.data, r->unparsed_uri.len);
    *p++ = ' ';

    if (r->http_version == NGX_HTTP_VERSION_20) {
        p = ngx_cpymem(p, "HTTP/2.0", sizeof("HTTP/2.0") - 1);
    } else if (r->http_version == NGX_HTTP_VERSION_11) {
        p = ngx_cpymem(p, "HTTP/1.1", sizeof("HTTP/1.1") - 1);
    } else {
        p = ngx_cpymem(p, "HTTP/1.0", sizeof("HTTP/1.0") - 1);
    }

    result.len = p - result.data;
    return result;
}


/*
 * Main variable lookup function.
 * Maps Apache mod_rewrite variable names to nginx equivalents.
 */
ngx_str_t
ngx_rewrite_lookup_variable(ngx_str_t *var, ngx_rewrite_ctx_t *ctx)
{
    ngx_http_request_t            *r = ctx->r;
    ngx_pool_t                    *pool = r->pool;
    ngx_str_t                      result = ngx_null_string;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_http_core_loc_conf_t      *clcf;
    u_char                        *p;
    struct tm                      tm;
    time_t                         now;
    ngx_str_t                      hdr_name;

    if (var->len == 0) {
        return result;
    }

    /* ENV:varname — look up nginx variable */
    if (var->len > 4
        && ngx_strncasecmp(var->data, (u_char *) "ENV:", 4) == 0)
    {
        ngx_str_t                 vname;
        ngx_uint_t                key;
        ngx_http_variable_value_t *vv;

        vname.data = var->data + 4;
        vname.len = var->len - 4;
        key = ngx_hash_strlow(vname.data, vname.data, vname.len);

        vv = ngx_http_get_variable(r, &vname, key);
        if (vv && !vv->not_found && vv->len > 0) {
            result.data = vv->data;
            result.len = vv->len;
        }
        return result;
    }

    /* HTTP:header — generic header lookup */
    if (var->len > 5
        && ngx_strncasecmp(var->data, (u_char *) "HTTP:", 5) == 0)
    {
        hdr_name.data = var->data + 5;
        hdr_name.len = var->len - 5;
        return ngx_rewrite_lookup_header(r, &hdr_name);
    }

    /* Fixed variable names — use length switch like Apache */
    switch (var->len) {

    case 4:
        if (ngx_strncasecmp(var->data, (u_char *) "TIME", 4) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 15);
            if (p) {
                result.len = ngx_sprintf(p, "%04d%02d%02d%02d%02d%02d",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec) - p;
                result.data = p;
            }
            return result;
        }
        break;

    case 5:
        if (ngx_strncasecmp(var->data, (u_char *) "HTTPS", 5) == 0) {
#if (NGX_SSL)
            if (r->connection->ssl) {
                ngx_str_set(&result, "on");
            } else {
                ngx_str_set(&result, "off");
            }
#else
            ngx_str_set(&result, "off");
#endif
            return result;
        }
        break;

    case 8:
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_DAY", 8) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 3);
            if (p) {
                result.len = ngx_sprintf(p, "%02d", tm.tm_mday) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_SEC", 8) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 3);
            if (p) {
                result.len = ngx_sprintf(p, "%02d", tm.tm_sec) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_MIN", 8) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 3);
            if (p) {
                result.len = ngx_sprintf(p, "%02d", tm.tm_min) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_MON", 8) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 3);
            if (p) {
                result.len = ngx_sprintf(p, "%02d", tm.tm_mon + 1) - p;
                result.data = p;
            }
            return result;
        }
        break;

    case 9:
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_HOST", 9) == 0) {
            if (r->headers_in.host) {
                result = r->headers_in.host->value;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "IS_SUBREQ", 9) == 0) {
            if (r->main != r) {
                ngx_str_set(&result, "true");
            } else {
                ngx_str_set(&result, "false");
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "PATH_INFO", 9) == 0) {
            /* In nginx rewrite phase there's no path_info yet */
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_HOUR", 9) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 3);
            if (p) {
                result.len = ngx_sprintf(p, "%02d", tm.tm_hour) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_WDAY", 9) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 2);
            if (p) {
                result.len = ngx_sprintf(p, "%d", tm.tm_wday) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "TIME_YEAR", 9) == 0) {
            now = ngx_time();
            ngx_localtime(now, &tm);
            p = ngx_pnalloc(pool, 5);
            if (p) {
                result.len = ngx_sprintf(p, "%04d",
                    tm.tm_year + 1900) - p;
                result.data = p;
            }
            return result;
        }
        break;

    case 10:
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_URL", 10) == 0) {
            /* Not commonly used, return empty */
            return result;
        }
        break;

    case 11:
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_NAME", 11) == 0) {
            cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
            if (cscf) {
                result = cscf->server_name;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REMOTE_ADDR", 11) == 0) {
            result = r->connection->addr_text;
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_ADDR", 11) == 0) {
            ngx_str_t  addr;
            u_char     sa[NGX_SOCKADDR_STRLEN];

            addr.data = sa;
            addr.len = NGX_SOCKADDR_STRLEN;

            if (ngx_connection_local_sockaddr(r->connection, &addr, 0)
                == NGX_OK)
            {
                result.data = ngx_pnalloc(pool, addr.len);
                if (result.data) {
                    ngx_memcpy(result.data, addr.data, addr.len);
                    result.len = addr.len;
                }
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "THE_REQUEST", 11) == 0) {
            return ngx_rewrite_build_the_request(r, pool);
        }
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_ACCEPT", 11) == 0) {
            hdr_name.data = (u_char *) "Accept";
            hdr_name.len = 6;
            return ngx_rewrite_lookup_header(r, &hdr_name);
        }
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_COOKIE", 11) == 0) {
            hdr_name.data = (u_char *) "Cookie";
            hdr_name.len = 6;
            return ngx_rewrite_lookup_header(r, &hdr_name);
        }
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_PORT", 11) == 0) {
            /* Extract port from local sockaddr */
            ngx_uint_t  port;
            struct sockaddr *sa;

            sa = r->connection->local_sockaddr;
            port = ngx_inet_get_port(sa);
            p = ngx_pnalloc(pool, 6);
            if (p) {
                result.len = ngx_sprintf(p, "%ui", port) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REMOTE_PORT", 11) == 0) {
            ngx_uint_t  port;
            struct sockaddr *sa;

            sa = r->connection->sockaddr;
            port = ngx_inet_get_port(sa);
            p = ngx_pnalloc(pool, 6);
            if (p) {
                result.len = ngx_sprintf(p, "%ui", port) - p;
                result.data = p;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REMOTE_HOST", 11) == 0) {
            /* In nginx, REMOTE_HOST = REMOTE_ADDR (no reverse DNS) */
            result = r->connection->addr_text;
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REQUEST_URI", 11) == 0) {
            result = r->unparsed_uri;
            return result;
        }
        break;

    case 12:
        if (ngx_strncasecmp(var->data, (u_char *) "QUERY_STRING", 12) == 0) {
            result = r->args;
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_REFERER", 12) == 0) {
            if (r->headers_in.referer) {
                result = r->headers_in.referer->value;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REMOTE_IDENT", 12) == 0) {
            /* Not available in nginx */
            return result;
        }
        break;

    case 13:
        if (ngx_strncasecmp(var->data, (u_char *) "DOCUMENT_ROOT", 13) == 0) {
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            if (clcf) {
                result = clcf->root;
            }
            return result;
        }
        break;

    case 14:
        if (ngx_strncasecmp(var->data, (u_char *) "REQUEST_METHOD", 14) == 0) {
            result = r->method_name;
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_FORWARDED", 14) == 0) {
            hdr_name.data = (u_char *) "Forwarded";
            hdr_name.len = 9;
            return ngx_rewrite_lookup_header(r, &hdr_name);
        }
        if (ngx_strncasecmp(var->data, (u_char *) "REQUEST_SCHEME", 14) == 0) {
#if (NGX_SSL)
            if (r->connection->ssl) {
                ngx_str_set(&result, "https");
            } else {
                ngx_str_set(&result, "http");
            }
#else
            ngx_str_set(&result, "http");
#endif
            return result;
        }
        break;

    case 15:
        if (ngx_strncasecmp(var->data, (u_char *) "HTTP_USER_AGENT", 15) == 0)
        {
            if (r->headers_in.user_agent) {
                result = r->headers_in.user_agent->value;
            }
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_PROTOCOL", 15) == 0)
        {
            result = r->http_protocol;
            return result;
        }
        if (ngx_strncasecmp(var->data, (u_char *) "SERVER_SOFTWARE", 15) == 0)
        {
            ngx_str_set(&result, NGINX_VER);
            return result;
        }
        if (ngx_strncasecmp(var->data,
            (u_char *) "SCRIPT_FILENAME", 15) == 0)
        {
            size_t root;
            ngx_str_t r_path;
            u_char *last;

            // Вызываем функцию для построения полного пути
            last = ngx_http_map_uri_to_path(r, &r_path, &root, 0);
            if (last == NULL) {
                clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
                if (r->uri.len > 1 && clcf != NULL) {
                    if (r->uri.data[0] == '/') {
                        ngx_str_t tmp_str = ngx_null_string;
                        tmp_str.data = ngx_pnalloc(r->pool, r->uri.len);
                        if (tmp_str.data){
                            ngx_memzero(tmp_str.data, r->uri.len);
                            ngx_memcpy(tmp_str.data, r->uri.data+1, r->uri.len-1);
                            tmp_str.len = r->uri.len-1;
                            ngx_str_t full_path = ngx_null_string;
                            full_path.data = ngx_pnalloc(r->pool, tmp_str.len + clcf->root.len + 1);
                            ngx_memcpy(full_path.data, clcf->root.data, clcf->root.len);
                            ngx_memcpy(full_path.data + clcf->root.len, tmp_str.data, tmp_str.len);
                            full_path.len = tmp_str.len + clcf->root.len + 1;
                            result = full_path;
                        } else {
                            result = r->uri;
                        }
                    } else {
                        ngx_str_t full_path = ngx_null_string;
                        full_path.data = ngx_pnalloc(r->pool, r->uri.len + clcf->root.len + 1);
                        ngx_memcpy(full_path.data, clcf->root.data, clcf->root.len);
                        ngx_memcpy(full_path.data + clcf->root.len, r->uri.data, r->uri.len);
                        full_path.len =r->uri.len + clcf->root.len + 1;
                        result = full_path;
                    }
                } else {
                    result = r->uri;
                }
            } else {
                r_path.len = last - r_path.data;
                r_path.data = r_path.data;
                result = r_path;
            }

            return result;
        }
        break;

    case 16:
        if (ngx_strncasecmp(var->data,
            (u_char *) "REQUEST_FILENAME", 16) == 0)
        {
            size_t root;
            ngx_str_t r_path;
            u_char *last;

            last = ngx_http_map_uri_to_path(r, &r_path, &root, 0);
            if (last == NULL) {
                clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
                if (r->uri.len > 1 && clcf != NULL) {
                    if (r->uri.data[0] == '/') {
                        ngx_str_t tmp_str = ngx_null_string;
                        tmp_str.data = ngx_pnalloc(r->pool, r->uri.len);
                        if (tmp_str.data){
                            ngx_memzero(tmp_str.data, r->uri.len);
                            ngx_memcpy(tmp_str.data, r->uri.data+1, r->uri.len-1);
                            tmp_str.len = r->uri.len-1;
                            ngx_str_t full_path = ngx_null_string;
                            full_path.data = ngx_pnalloc(r->pool, tmp_str.len + clcf->root.len + 1);
                            ngx_memcpy(full_path.data, clcf->root.data, clcf->root.len);
                            ngx_memcpy(full_path.data + clcf->root.len, tmp_str.data, tmp_str.len);
                            full_path.len = tmp_str.len + clcf->root.len + 1;
                            result = full_path;
                        } else {
                            result = r->uri;
                        }
                    } else {
                        ngx_str_t full_path = ngx_null_string;
                        full_path.data = ngx_pnalloc(r->pool, r->uri.len + clcf->root.len + 1);
                        ngx_memcpy(full_path.data, clcf->root.data, clcf->root.len);
                        ngx_memcpy(full_path.data + clcf->root.len, r->uri.data, r->uri.len);
                        full_path.len =r->uri.len + clcf->root.len + 1;
                        result = full_path;
                    }
                } else {
                    result = r->uri;
                }
            } else {
                r_path.len = last - r_path.data;
                r_path.data = r_path.data;
                result = r_path;
            }

            return result;
        }
        if (ngx_strncasecmp(var->data,
            (u_char *) "CONN_REMOTE_ADDR", 16) == 0)
        {
            result = r->connection->addr_text;
            return result;
        }
        break;

    case 21:
        if (ngx_strncasecmp(var->data,
            (u_char *) "HTTP_PROXY_CONNECTION", 21) == 0)
        {
            hdr_name.data = (u_char *) "Proxy-Connection";
            hdr_name.len = 16;
            return ngx_rewrite_lookup_header(r, &hdr_name);
        }
        break;
    }

    return result;
}
