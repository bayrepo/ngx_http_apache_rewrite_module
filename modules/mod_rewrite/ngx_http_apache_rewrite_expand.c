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
 * ngx_http_apache_rewrite_expand.c
 *
 * String expansion for Apache mod_rewrite substitutions.
 * Handles: \\x (escape), $N (rule backrefs), %N (cond backrefs),
 *          %{VARNAME} (variables), ${MAP:KEY|DEFAULT} (map lookups).
 */

#include "ngx_http_apache_rewrite_engine.h"


/*
 * Result fragment linked list for efficient string building.
 */
typedef struct ngx_rewrite_result_s  ngx_rewrite_result_t;

struct ngx_rewrite_result_s {
    ngx_rewrite_result_t  *next;
    u_char                *data;
    size_t                 len;
};

#define SMALL_EXPANSION  5


/*
 * Find closing curly brace with nesting support.
 */
static u_char *
ngx_rewrite_find_closing_curly(u_char *s, u_char *end)
{
    ngx_uint_t depth = 1;

    for (; s < end; s++) {
        if (*s == '}') {
            if (--depth == 0) {
                return s;
            }
        } else if (*s == '{') {
            depth++;
        }
    }

    return NULL;
}


/*
 * Find a character inside curly braces (at depth 1).
 */
static u_char *
ngx_rewrite_find_char_in_curlies(u_char *s, u_char *end, u_char c)
{
    ngx_uint_t depth = 1;

    for (; s < end; s++) {
        if (*s == c && depth == 1) {
            return s;
        }
        if (*s == '}') {
            if (--depth == 0) {
                return NULL;
            }
        } else if (*s == '{') {
            depth++;
        }
    }

    return NULL;
}


/*
 * Expand a substitution string.
 *
 * Port of Apache do_expand() (mod_rewrite.c:2417-2677).
 * Single-pass expansion for security.
 */
ngx_str_t
ngx_rewrite_expand(ngx_str_t *input, ngx_rewrite_ctx_t *ctx,
    ngx_rewrite_rule_t *rule, ngx_pool_t *pool)
{
    ngx_str_t                          result_str;
    ngx_rewrite_result_t               sresult[SMALL_EXPANSION];
    ngx_rewrite_result_t              *result, *current;
    ngx_uint_t                         spc = 0;
    u_char                            *p, *inp_end, *c;
    size_t                             span, outlen;
    ngx_http_apache_rewrite_srv_conf_t *sconf;

    if (input->len == 0) {
        ngx_str_null(&result_str);
        return result_str;
    }

    inp_end = input->data + input->len;

    /* Find first special character */
    for (p = input->data; p < inp_end; p++) {
        if (*p == '\\' || *p == '$' || *p == '%') {
            break;
        }
    }

    span = p - input->data;

    /* Fast path: no specials */
    if (p >= inp_end) {
        result_str.data = ngx_pnalloc(pool, input->len);
        if (result_str.data == NULL) {
            ngx_str_null(&result_str);
            return result_str;
        }
        ngx_memcpy(result_str.data, input->data, input->len);
        result_str.len = input->len;
        return result_str;
    }

    /* Initialize result list */
    result = current = &sresult[spc++];
    current->next = NULL;
    current->data = input->data;
    current->len = span;
    outlen = span;

    /* Process specials */
    do {
        /* Advance to next node if current has content */
        if (current->len) {
            if (spc < SMALL_EXPANSION) {
                current->next = &sresult[spc++];
            } else {
                current->next = ngx_palloc(pool,
                                           sizeof(ngx_rewrite_result_t));
                if (current->next == NULL) {
                    ngx_str_null(&result_str);
                    return result_str;
                }
            }
            current = current->next;
            current->next = NULL;
            current->len = 0;
        }

        /* Escaped character: \x → literal x */
        if (*p == '\\') {
            current->len = 1;
            outlen++;
            p++;
            if (p >= inp_end) {
                current->data = p - 1;
                break;
            }
            current->data = p;
            p++;
        }

        /* Variable or map lookup: %{...} or ${...} */
        else if (p + 1 < inp_end && p[1] == '{') {
            u_char *endp;

            endp = ngx_rewrite_find_closing_curly(p + 2, inp_end);
            if (endp == NULL) {
                /* No closing brace, copy literally */
                current->len = 2;
                current->data = p;
                outlen += 2;
                p += 2;
            }
            /* %{VARNAME} — variable lookup */
            else if (*p == '%') {
                ngx_str_t vname, val;

                vname.data = p + 2;
                vname.len = endp - (p + 2);
                val = ngx_rewrite_lookup_variable(&vname, ctx);

                current->len = val.len;
                current->data = val.data;
                outlen += val.len;
                p = endp + 1;
            }
            /* ${MAP:KEY|DEFAULT} — map lookup */
            else { /* *p == '$' */
                u_char *key_start;

                key_start = ngx_rewrite_find_char_in_curlies(p + 2, endp, ':');
                if (key_start == NULL) {
                    /* No colon, copy literally */
                    current->len = 2;
                    current->data = p;
                    outlen += 2;
                    p += 2;
                } else {
                    ngx_str_t map_name, map_key, map_dflt, map_result;
                    ngx_str_t key_expanded;
                    u_char *dflt_start;

                    map_name.data = p + 2;
                    map_name.len = key_start - (p + 2);

                    key_start++; /* skip ':' */

                    /* Find default separator '|' */
                    dflt_start = ngx_rewrite_find_char_in_curlies(
                        key_start, endp, '|');

                    if (dflt_start) {
                        map_key.data = key_start;
                        map_key.len = dflt_start - key_start;
                        map_dflt.data = dflt_start + 1;
                        map_dflt.len = endp - (dflt_start + 1);
                    } else {
                        map_key.data = key_start;
                        map_key.len = endp - key_start;
                        ngx_str_null(&map_dflt);
                    }

                    /* Recursively expand the key */
                    key_expanded = ngx_rewrite_expand(&map_key, ctx,
                                                      rule, pool);

                    sconf = ngx_http_get_module_srv_conf(ctx->r,
                        ngx_http_apache_rewrite_module);

                    map_result = ngx_rewrite_lookup_map(ctx->r, sconf,
                        &map_name, &key_expanded);

                    if (map_result.len == 0 && map_dflt.len > 0) {
                        map_result = ngx_rewrite_expand(&map_dflt, ctx,
                                                        rule, pool);
                    }

                    current->len = map_result.len;
                    current->data = map_result.data;
                    outlen += map_result.len;
                    p = endp + 1;
                }
            }
        }

        /* Backreference: $N or %N */
        else if (p + 1 < inp_end
                 && p[1] >= '0' && p[1] <= '9')
        {
            ngx_int_t n = p[1] - '0';
            ngx_rewrite_backref_t *bri;

            if (*p == '$') {
                bri = &ctx->briRR;
            } else {
                bri = &ctx->briRC;
            }

            if (bri->source.data
                && n < bri->ncaptures
                && bri->ovector[n * 2] >= 0
                && bri->ovector[n * 2 + 1] > bri->ovector[n * 2])
            {
                span = bri->ovector[n * 2 + 1] - bri->ovector[n * 2];
                current->len = span;
                current->data = bri->source.data + bri->ovector[n * 2];
                outlen += span;
            }

            p += 2;
        }

        /* Not a special, copy literally */
        else {
            current->len = 1;
            current->data = p;
            outlen++;
            p++;
        }

        /* Find next stretch of non-special characters */
        if (p < inp_end) {
            u_char *start = p;
            while (p < inp_end && *p != '\\' && *p != '$' && *p != '%') {
                p++;
            }
            span = p - start;
            if (span > 0) {
                if (current->len) {
                    if (spc < SMALL_EXPANSION) {
                        current->next = &sresult[spc++];
                    } else {
                        current->next = ngx_palloc(pool,
                            sizeof(ngx_rewrite_result_t));
                        if (current->next == NULL) {
                            ngx_str_null(&result_str);
                            return result_str;
                        }
                    }
                    current = current->next;
                    current->next = NULL;
                }
                current->data = start;
                current->len = span;
                outlen += span;
            }
        }

    } while (p < inp_end);

    /* Assemble result */
    c = ngx_pnalloc(pool, outlen);
    if (c == NULL) {
        ngx_str_null(&result_str);
        return result_str;
    }

    result_str.data = c;
    result_str.len = outlen;

    current = result;
    while (current) {
        if (current->len) {
            ngx_memcpy(c, current->data, current->len);
            c += current->len;
        }
        current = current->next;
    }

    return result_str;
}
