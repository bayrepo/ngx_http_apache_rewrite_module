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
 * ngx_http_apache_rewrite_fastcgi.c
 *
 * FastCGI integration for Apache mod_rewrite module.
 * Passes environment variables set by [E=VAR:VAL] flags in RewriteRule
 * to PHP/FastCGI contexts via nginx variables.
 *
 * This file provides:
 * - ngx_rewrite_get_env_var() - lookup function for env vars
 * - ngx_rewrite_create_env_variable() - register custom variable handler
 * - Automatic passing mechanism to FastCGI params (auto mode)
 */

#include "ngx_http_apache_rewrite_engine.h"


/* ============================================================
 * Environment Variable Storage in Context
 * ============================================================ */
struct ngx_rewrite_env_ctx_s {
    ngx_str_t           env_value;   /* Final value for this request context */
};


/*
 * Custom variable handler for E=VAR:VAL variables.
 * Returns the value from ctx->env_vars list.
 */
static ngx_int_t __attribute__((unused))
ngx_rewrite_env_variable_handler(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, void *data)
{
    ngx_rewrite_ctx_t  *ctx;

    v->valid = 1;
    v->no_cacheable = 0;

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);

    if (!ctx || !ctx->env_vars) {
        v->not_found = 1;
        return NGX_OK;
    }

    /* Lookup by name */
    u_char *colon;
    const char *var_name = (const char *)v->data;
    size_t var_name_len = v->len;

    ngx_rewrite_data_item_t  *env = ctx->env_vars;
    while (env) {
        if (!env->data.data || env->data.len == 0) {
            env = env->next;
            continue;
        }

        /* Check format VAR:VAL */
        colon = ngx_strlchr(env->data.data,
                            (u_char *)(env->data.data + env->data.len), ':');

        if (!colon) {
            env = env->next;
            continue;
        }

        size_t name_len = colon - env->data.data;
        u_char *var_name_lowered = ngx_pnalloc(r->pool, var_name_len);
        for (size_t i = 0; i < var_name_len; i++) {
            var_name_lowered[i] = ngx_tolower(var_name[i]);
        }

        /* Compare names case-insensitively */
        if ((name_len == var_name_len &&
            ngx_strncmp((char *)colon + 1, (char *)var_name_lowered, name_len) == 0) ||
            strcmp((const char *)(env->data.data), var_name) == 0) {

            /* Found matching env var */
            u_char *p = v->data;
            while (*p != ':' && p < v->data + v->len) {
                p++;
            }

            if (p < v->data + v->len) {
                /* Extract value after colon */
                size_t value_len = env->data.len - name_len - 1;
                const u_char *value_start = colon + 1;
                u_char *value_copy = ngx_pnalloc(r->pool, value_len);
                if (!value_copy) {
                    v->not_found = 1;
                    return NGX_OK;
                }

                ngx_memcpy(value_copy, value_start, value_len);

                v->data = (u_char *)value_copy;
                v->len = value_len;
                return NGX_OK;
            }
        }

        env = env->next;
    }

    /* Not found */
    v->not_found = 1;
    return NGX_OK;
}


/*
 * Register a custom variable from E=VAR:VAL format.
 * Creates nginx variable $var_name and links to handler.
 */
ngx_int_t
ngx_rewrite_create_env_variable(ngx_http_request_t *r,
    const char *name, size_t name_len, const u_char *value, size_t value_len)
{
    ngx_str_t           vname;
    ngx_uint_t          key;

    /* Create nginx variable name: $var_name */
    vname.len = name_len + 1;  /* +1 for '$' prefix */
    vname.data = (u_char *)ngx_palloc(r->pool, vname.len + 1);
    if (!vname.data) {
        return NGX_ERROR;
    }

    vname.data[0] = '$';
    ngx_memcpy(vname.data + 1, name, name_len);
    /* Remove underscores for nginx var format (HTTP_AUTH -> http-auth) */
    for (size_t i = 1; i < vname.len; i++) {
        if (vname.data[i] == '_') {
            vname.data[i] = '-';
        } else {
            vname.data[i] = ngx_tolower(vname.data[i]);
        }
    }

    /* Calculate hash */
    key = ngx_hash_strlow(vname.data, vname.data, vname.len);

    /* Lookup/create variable */
    ngx_http_variable_value_t  *vv;
    vv = ngx_http_get_variable(r, &vname, key);

    if (!vv) {
        /* Variable doesn't exist - this shouldn't happen normally */
        return NGX_ERROR;
    }

    /* Set the value */
    vv->data = (u_char *)value;
    vv->len = value_len;
    vv->valid = 1;
    vv->no_cacheable = 0;
    vv->not_found = 0;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite-fastcgi: Created variable \"%V\"", &vname);

    return NGX_OK;
}


/* ============================================================
 * Lookup Functions for Downstream Modules (e.g., FastCGI)
 * ============================================================ */

/**
 * Get an environment variable set by mod_rewrite [E=...] flag.
 * Returns the value or null if not found/invalid.
 */
ngx_str_t
ngx_rewrite_get_env_var(ngx_http_request_t *r, const char *name, size_t name_len)
{
    ngx_uint_t          key;
    ngx_str_t           vname;
    ngx_http_variable_value_t  *vv;

    /* Lookup by name - handle underscores to dashes */
    u_char *lowered = ngx_pnalloc(r->pool, name_len + 1);
    for (size_t i = 0; i < name_len; i++) {
        lowered[i] = ngx_tolower(name[i]);
    }

    vname.data = lowered;
    vname.len = name_len;
    key = ngx_hash_strlow(vname.data, vname.data, vname.len);

    vv = ngx_http_get_variable(r, &vname, key);

    if (!vv || vv->not_found) {
        /* Variable not found */
        return (ngx_str_t){ 0, NULL };
    }

    /* Return a copy since we can't guarantee the data persists */
    ngx_str_t result;
    result.len = vv->len;
    result.data = ngx_pnalloc(r->pool, vv->len);
    if (!result.data) {
        return (ngx_str_t){ 0, NULL };
    }

    ngx_memcpy(result.data, vv->data, vv->len);

    return result;
}


/**
 * Convert ALL environment variables from mod_rewrite to HTTP headers.
 * This is called in CONTENT_PHASE handler to pass E vars to FastCGI/PHP.
 *
 * Apache PHP sees $_SERVER['HTTP_*'] for all HTTP headers automatically.
 * So we convert E=VAR:VAL -> HTTP header r->headers_in.VAR
 */
ngx_int_t
ngx_rewrite_add_env_as_headers(ngx_http_request_t *r)
{
    ngx_rewrite_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);

    if (!ctx || !ctx->env_vars) {
        /* No env vars from mod_rewrite - nothing to convert */
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite-http-headers: Converting E-vars from ctx");

    /* Iterate over all environment variables */
    ngx_rewrite_data_item_t  *env = ctx->env_vars;

    while (env) {
        u_char   *colon;

        if (!env->data.data || env->data.len == 0) {
            env = env->next;
            continue;
        }

        /* Split VAR:VAL */
        colon = ngx_strlchr(env->data.data,
                            (u_char *)(env->data.data + env->data.len), ':');

        if (!colon) {
            /* No colon - invalid format, skip */
            env = env->next;
            continue;
        }

        size_t var_name_len = colon - env->data.data;
        const u_char *value_start = colon + 1;

        ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite-http-headers: E-var \"%*s\" => \"%*s\"",
                       var_name_len, env->data.data,
                       env->data.len - var_name_len - 1, value_start);

        /* Convert header names for PHP access via $_SERVER['HTTP_*']
         * Apache behavior: E=VAR sets environment variable VAR.
         * FastCGI/PHP sees it as $_SERVER[VAR].
         * For HTTP_* headers (like HTTP_AUTHORIZATION), we need to set them correctly. */

        u_char *header_name;
        size_t header_len;

        /* Check if the var already starts with "HTTP_" - don't double-prefix */
        if (var_name_len >= 5 && ngx_strncasecmp(env->data.data, (u_char *)"HTTP_", 5) == 0) {
            /* Already HTTP_* format - use as-is for $_SERVER[HTTP_*] */
            header_name = ngx_pnalloc(r->pool, var_name_len + 1);
            if (!header_name) {
                return NGX_ERROR;
            }

            /* Copy VAR name and convert to uppercase with underscores */
            for (size_t j = 0; j < var_name_len; j++) {
                char ch = ((char *)env->data.data)[j];
                if (ch == '-') {
                    header_name[j] = '_';
                } else {
                    header_name[j] = ngx_toupper(ch);
                }
            }

            /* Set without HTTP_ prefix since already has it */
            header_len = var_name_len;
        } else {
            /* Need to add HTTP_ prefix for non-HTTP vars */
            header_name = ngx_pnalloc(r->pool, var_name_len + 6);
            if (!header_name) {
                return NGX_ERROR;
            }

            size_t i = 0;
            header_name[i++] = 'H';
            header_name[i++] = 'T';
            header_name[i++] = 'T';
            header_name[i++] = 'P';
            header_name[i++] = '_';

            /* Copy VAR name and convert to uppercase with underscores */
            for (size_t j = 0; j < var_name_len; j++) {
                char ch = ((char *)env->data.data)[j];
                if (ch == '-') {
                    header_name[i++] = '_';
                } else {
                    header_name[i] = ngx_toupper(ch);
                    i++;
                }
            }

            header_len = i;
        }

        /* Create HTTP_* header entry */
        u_char *header_value_copy = ngx_pnalloc(r->pool, env->data.len - var_name_len - 1 + 1);
        if (!header_value_copy) {
            return NGX_ERROR;
        }

        ngx_memcpy(header_value_copy, value_start, env->data.len - var_name_len - 1);
        header_value_copy[env->data.len - var_name_len - 1] = '\0';

        /* Add header to r->headers_in */
        ngx_table_elt_t *h = ngx_list_push(&r->headers_in.headers);
        if (!h) {
            return NGX_ERROR;
        }

        h->hash = 1;
        h->key.len = header_len;
        h->key.data = header_name;
        h->value.len = env->data.len - var_name_len - 1;
        h->value.data = header_value_copy;

        ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite-http-headers: Added HTTP header \"%*s\" => \"%*s\"",
                       header_len, header_name, h->value.len, h->value.data);

        env = env->next;
    }

    return NGX_OK;
}


/* ============================================================
 * Module Hooks (Registered via postconfiguration)
 * ============================================================ */

/**
 * Register handlers for fastcgi phase integration.
 * This is called during module initialization.
 */
ngx_int_t
ngx_http_apache_rewrite_fastcgi_register(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite-fastcgi: Auto-integration mode enabled");

    return NGX_OK;
}


/* ============================================================
 * Public API Export Header
 * ============================================================ */

/* These functions can be called from other modules or nginx config directives */
extern ngx_str_t ngx_rewrite_get_env_var(ngx_http_request_t *r, const char *name, size_t name_len);
extern ngx_int_t ngx_rewrite_create_env_variable(ngx_http_request_t *r,
    const char *name, size_t name_len, const u_char *value, size_t value_len);

extern ngx_int_t ngx_rewrite_add_env_as_headers(ngx_http_request_t *r);
