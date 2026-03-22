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
 * ngx_http_apache_rewrite_map.c
 *
 * RewriteMap implementations for nginx Apache rewrite module.
 * Phase 1: internal functions (tolower, toupper, escape, unescape).
 * Phase 2 will add txt, rnd, prg maps (if needed).
 */

#include "ngx_http_apache_rewrite_engine.h"


/*
 * int:tolower — convert string to lowercase
 */
ngx_str_t
ngx_rewrite_map_tolower(ngx_pool_t *pool, ngx_str_t key)
{
    ngx_str_t  result;
    u_char    *p;

    result.data = ngx_pnalloc(pool, key.len);
    if (result.data == NULL) {
        ngx_str_null(&result);
        return result;
    }

    result.len = key.len;
    for (p = result.data; key.len--; ) {
        *p++ = ngx_tolower(*key.data++);
    }

    return result;
}


/*
 * int:toupper — convert string to uppercase
 */
ngx_str_t
ngx_rewrite_map_toupper(ngx_pool_t *pool, ngx_str_t key)
{
    ngx_str_t  result;
    u_char    *p;
    ngx_uint_t i;

    result.data = ngx_pnalloc(pool, key.len);
    if (result.data == NULL) {
        ngx_str_null(&result);
        return result;
    }

    result.len = key.len;
    p = result.data;
    for (i = 0; i < key.len; i++) {
        u_char ch = key.data[i];
        if (ch >= 'a' && ch <= 'z') {
            ch -= 32;
        }
        *p++ = ch;
    }

    return result;
}


/*
 * int:escape — percent-encode string (URI-safe encoding)
 */
ngx_str_t
ngx_rewrite_map_escape(ngx_pool_t *pool, ngx_str_t key)
{
    ngx_str_t  result;
    uintptr_t  escaped_len;

    escaped_len = ngx_escape_uri(NULL, key.data, key.len,
                                 NGX_ESCAPE_ARGS);
    result.len = key.len + 2 * escaped_len;
    result.data = ngx_pnalloc(pool, result.len);
    if (result.data == NULL) {
        ngx_str_null(&result);
        return result;
    }

    if (escaped_len) {
        ngx_escape_uri(result.data, key.data, key.len, NGX_ESCAPE_ARGS);
    } else {
        ngx_memcpy(result.data, key.data, key.len);
    }

    return result;
}


/*
 * int:unescape — percent-decode string
 */
ngx_str_t
ngx_rewrite_map_unescape(ngx_pool_t *pool, ngx_str_t key)
{
    ngx_str_t  result;
    u_char    *dst;

    result.data = ngx_pnalloc(pool, key.len + 1);
    if (result.data == NULL) {
        ngx_str_null(&result);
        return result;
    }

    dst = result.data;
    ngx_memcpy(dst, key.data, key.len);
    dst[key.len] = '\0';

    ngx_unescape_uri(&dst, &result.data, key.len, 0);
    /* After unescape, dst points past written data, result.data is start */
    /* Actually ngx_unescape_uri modifies both pointers. Re-do properly: */
    result.data = ngx_pnalloc(pool, key.len + 1);
    if (result.data == NULL) {
        ngx_str_null(&result);
        return result;
    }

    ngx_memcpy(result.data, key.data, key.len);
    result.data[key.len] = '\0';

    dst = result.data;
    {
        u_char *src = result.data;
        ngx_unescape_uri(&dst, &src, key.len, 0);
    }
    result.len = dst - result.data;

    return result;
}


/*
 * Lookup a named map and return the value for the given key.
 * In Phase 1, only internal (int:) maps are supported.
 */
ngx_str_t
ngx_rewrite_lookup_map(ngx_http_request_t *r,
    ngx_http_apache_rewrite_srv_conf_t *sconf,
    ngx_str_t *mapname, ngx_str_t *key)
{
    ngx_str_t                  result = ngx_null_string;
    ngx_rewrite_map_entry_t   *maps;
    ngx_uint_t                 i;

    if (sconf == NULL || sconf->maps == NULL || sconf->maps->nelts == 0) {
        return result;
    }

    maps = sconf->maps->elts;
    for (i = 0; i < sconf->maps->nelts; i++) {
        if (maps[i].name.len == mapname->len
            && ngx_strncasecmp(maps[i].name.data, mapname->data,
                               mapname->len) == 0)
        {
            if (maps[i].type == MAPTYPE_INT && maps[i].func) {
                result = maps[i].func(r->pool, *key);
            }
            /* txt, rnd, prg maps — Phase 2 */
            break;
        }
    }

    return result;
}
