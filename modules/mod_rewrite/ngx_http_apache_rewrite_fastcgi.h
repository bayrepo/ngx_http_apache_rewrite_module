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
 * ngx_http_apache_rewrite_fastcgi.h
 *
 * FastCGI integration header for Apache mod_rewrite compatibility module.
 */

#ifndef _NGX_HTTP_APACHE_REWRITE_FASTCGI_H_
#define _NGX_HTTP_APACHE_REWRITE_FASTCGI_H_

#include <ngx_core.h>
#include <ngx_http.h>


/* ============================================================
 * Public API Functions
 * ============================================================ */

/**
 * Lookup an environment variable set by [E=VAR:VAL] flag.
 * Returns the value or ngx_null_string if not found.
 *
 * @param r nginx request
 * @param name variable name (e.g., "HTTP_AUTHORIZATION")
 * @param name_len length of name
 * @return ngx_str_t containing the value, or ngx_null_string on failure
 */
extern ngx_str_t ngx_rewrite_get_env_var(ngx_http_request_t *r, const char *name, size_t name_len);


/**
 * Create/register a custom variable for FastCGI.
 * Sets up nginx variable $var_name with given value.
 *
 * @param r nginx request
 * @param name variable name (without prefix)
 * @param name_len length of name
 * @param value value string to set
 * @param value_len length of value
 * @return NGX_OK on success, NGX_ERROR on failure
 */
extern ngx_int_t ngx_rewrite_create_env_variable(ngx_http_request_t *r,
    const char *name, size_t name_len, const u_char *value, size_t value_len);


/**
 * Automatically add ALL [E=...] environment variables as HTTP headers.
 * Called in CONTENT_PHASE to pass vars without manual nginx config.
 * PHP/FastCGI will see these as $_SERVER['HTTP_*'] automatically.
 *
 * @param r nginx request containing ctx->env_vars
 * @return NGX_OK on success, NGX_ERROR on failure
 */

extern ngx_int_t ngx_rewrite_add_env_as_headers(ngx_http_request_t *r);


/**
 * Register module hooks for FastCGI integration.
 * Called during postconfiguration.
 *
 * @param cf nginx configuration context
 * @return NGX_OK on success, NGX_ERROR on failure
 */
extern ngx_int_t ngx_http_apache_rewrite_fastcgi_register(ngx_http_request_t *r);


/* ============================================================
 * Compile-time Options
 * ============================================================ */

/* Enable FastCGI integration - defines NGX_FASTCGI_INTEGRATION_ENABLED in module.c */
#define NGX_FASTCGI_INTEGRATION_ENABLED   1

/* Mark ngx_http_fastcgi_module.h as available (defined by nginx core) */
#ifdef NGX_HTTP_FASTCGI_MODULE
#define NGX_FASTCGI_MODULE_PRESENT    1
#endif


#endif /* _NGX_HTTP_APACHE_REWRITE_FASTCGI_H_ */
