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
 * ngx_http_apache_rewrite_module.c
 *
 * Main nginx module file for Apache mod_rewrite compatibility.
 * Provides directives: RewriteEngine, RewriteRule, RewriteCond,
 *                      RewriteBase, RewriteOptions, RewriteMap
 *
 * Registers phase handlers for SERVER_REWRITE and REWRITE phases.
 */

#include "ngx_http_apache_rewrite_engine.h"

/* FastCGI integration header (if available) */
#include "ngx_http_apache_rewrite_fastcgi.h"


/* Forward declarations */
static void *ngx_http_apache_rewrite_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_apache_rewrite_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child);
static void *ngx_http_apache_rewrite_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_apache_rewrite_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_apache_rewrite_postconfiguration(ngx_conf_t *cf);

static char *ngx_http_rewrite_engine(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_rule(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_cond(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_base(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_options(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_map(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static char *ngx_http_htaccess_enable(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_htaccess_name(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_fallback_to_index(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void ngx_htaccess_update_cache(ngx_http_request_t *r, ngx_str_t file_path, time_t mtime, ngx_http_apache_rewrite_loc_conf_t *parsed_rules);
static ngx_htaccess_pool_cleanup_ctx_t *ngx_htaccess_get_from_pool_cleanup(ngx_http_request_t *r);
static htaccess_entry_t *ngx_htaccess_create_in_pool_cleanup(ngx_http_request_t *r, ngx_str_t file_path, time_t mtime,
    ngx_http_apache_rewrite_loc_conf_t *parsed_rules, ngx_htaccess_pool_cleanup_ctx_t *pool);
static void ngx_htaccess_cleanup(void *data);
static ngx_http_apache_rewrite_loc_conf_t * ngx_htaccess_parse_file_from_ha(ngx_http_request_t *r, ngx_str_t *file_path);
static void ngx_rewrite_strip_brackets(ngx_str_t *s);
static ngx_int_t ngx_htaccess_build_path(ngx_http_request_t *r, u_char *docroot, size_t docroot_len,
    ngx_str_t *path, u_char *htaccess_name, size_t htaccess_len);
static ngx_int_t ngx_htaccess_search_upward(ngx_http_request_t *r, u_char *docroot, size_t docroot_len, ngx_str_t *current_path,
    u_char *htaccess_name, size_t htaccess_name_len, ngx_str_t *found_path);

static ngx_int_t ngx_http_apache_rewrite_server_handler(
    ngx_http_request_t *r);
static ngx_int_t ngx_http_apache_rewrite_location_handler(
    ngx_http_request_t *r);

static ngx_int_t
ngx_http_apache_rewrite_url_register_hook_with_fallback(ngx_http_request_t *r);

/* --- Flag parsing helpers --- */

static ngx_int_t ngx_http_rewrite_parse_rule_flags(ngx_pool_t *pool,
    ngx_str_t *flags_str, ngx_rewrite_rule_t *rule);
static ngx_int_t ngx_http_rewrite_parse_cond_flags(ngx_conf_t *cf,
    ngx_str_t *flags_str, ngx_rewrite_cond_t *cond);


/*
 * Directives
 */
static ngx_command_t ngx_http_apache_rewrite_commands[] = {

    { ngx_string("RewriteEngine"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_rewrite_engine,
      0,
      0,
      NULL },

    { ngx_string("RewriteRule"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
      ngx_http_rewrite_rule,
      0,
      0,
      NULL },

    { ngx_string("RewriteCond"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
      ngx_http_rewrite_cond,
      0,
      0,
      NULL },

    { ngx_string("RewriteBase"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_rewrite_base,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("RewriteOptions"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_rewrite_options,
      0,
      0,
      NULL },

    { ngx_string("RewriteMap"),
      NGX_HTTP_SRV_CONF|NGX_CONF_TAKE2,
      ngx_http_rewrite_map,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("HtaccessEnable"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
      ngx_http_htaccess_enable,
      0,
      0,
      NULL },

    { ngx_string("HtaccessName"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_htaccess_name,
      0,
      0,
      NULL },

    { ngx_string("FallbackToIndex"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
      ngx_http_fallback_to_index,
      0,
      0,
      NULL },

    ngx_null_command
};


/*
 * Module context
 */
static ngx_http_module_t ngx_http_apache_rewrite_module_ctx = {
    NULL,                                       /* preconfiguration */
    ngx_http_apache_rewrite_postconfiguration,  /* postconfiguration */

    NULL,                                       /* create main configuration */
    NULL,                                       /* init main configuration */

    ngx_http_apache_rewrite_create_srv_conf,    /* create server configuration */
    ngx_http_apache_rewrite_merge_srv_conf,     /* merge server configuration */

    ngx_http_apache_rewrite_create_loc_conf,    /* create location configuration */
    ngx_http_apache_rewrite_merge_loc_conf      /* merge location configuration */
};


/*
 * Module definition
 */
ngx_module_t ngx_http_apache_rewrite_module = {
    NGX_MODULE_V1,
    &ngx_http_apache_rewrite_module_ctx,        /* module context */
    ngx_http_apache_rewrite_commands,            /* module directives */
    NGX_HTTP_MODULE,                             /* module type */
    NULL,                                        /* init master */
    NULL,                                        /* init module */
    NULL,                                        /* init process */
    NULL,                                        /* init thread */
    NULL,                                        /* exit thread */
    NULL,                                        /* exit process */
    NULL,                                        /* exit master */
    NGX_MODULE_V1_PADDING
};


/* ============================================================
 *   Configuration create/merge
 * ============================================================ */

static void *
ngx_http_apache_rewrite_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_apache_rewrite_srv_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_apache_rewrite_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->state = ENGINE_DISABLED;
    conf->options = OPTION_NONE;

    conf->rules = ngx_array_create(cf->pool, 4, sizeof(ngx_rewrite_rule_t));
    conf->pending_conds = ngx_array_create(cf->pool, 4,
                                           sizeof(ngx_rewrite_cond_t));
    conf->maps = ngx_array_create(cf->pool, 2,
                                  sizeof(ngx_rewrite_map_entry_t));

    if (conf->rules == NULL || conf->pending_conds == NULL
        || conf->maps == NULL)
    {
        return NULL;
    }

    conf->htaccess_enable = 0;
    conf->htaccess_enable_set = 0;

    conf->fallback_to_index = 1;

    return conf;
}


static char *
ngx_http_apache_rewrite_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child)
{
    ngx_http_apache_rewrite_srv_conf_t *prev = parent;
    ngx_http_apache_rewrite_srv_conf_t *conf = child;

    if (!conf->state_set) {
        conf->state = prev->state;
    }

    if (!conf->options_set) {
        conf->options = prev->options;
    }

    if (!conf->htaccess_enable_set) {
        conf->htaccess_enable = prev->htaccess_enable;
    }

    if (!conf->fallback_to_index_set){
        conf->fallback_to_index = prev->fallback_to_index;
    }

    /* Inherit rules if child has none and Inherit option is set */
    if (conf->rules->nelts == 0
        && prev->rules->nelts > 0
        && (conf->options & OPTION_INHERIT))
    {
        ngx_uint_t         i;
        ngx_rewrite_rule_t *src, *dst;

        src = prev->rules->elts;
        for (i = 0; i < prev->rules->nelts; i++) {
            dst = ngx_array_push(conf->rules);
            if (dst == NULL) {
                return NGX_CONF_ERROR;
            }
            *dst = src[i];
        }
    }

    return NGX_CONF_OK;
}


static void *
ngx_http_apache_rewrite_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_apache_rewrite_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_apache_rewrite_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->state = ENGINE_DISABLED;
    conf->options = OPTION_NONE;

    conf->rules = ngx_array_create(cf->pool, 4, sizeof(ngx_rewrite_rule_t));
    conf->pending_conds = ngx_array_create(cf->pool, 4,
                                           sizeof(ngx_rewrite_cond_t));

    if (conf->rules == NULL || conf->pending_conds == NULL) {
        return NULL;
    }

    ngx_str_null(&conf->baseurl);

    return conf;
}


static char *
ngx_http_apache_rewrite_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child)
{
    ngx_http_apache_rewrite_loc_conf_t *prev = parent;
    ngx_http_apache_rewrite_loc_conf_t *conf = child;

    if (!conf->state_set) {
        conf->state = prev->state;
    }

    if (!conf->options_set) {
        conf->options = prev->options;
    }

    if (!conf->baseurl_set && prev->baseurl_set) {
        conf->baseurl = prev->baseurl;
        conf->baseurl_set = 1;
    }

    /* Inherit rules if child has none and Inherit option is set */
    if (conf->rules->nelts == 0
        && prev->rules->nelts > 0
        && (conf->options & OPTION_INHERIT))
    {
        ngx_uint_t         i;
        ngx_rewrite_rule_t *src, *dst;

        src = prev->rules->elts;
        for (i = 0; i < prev->rules->nelts; i++) {
            dst = ngx_array_push(conf->rules);
            if (dst == NULL) {
                return NGX_CONF_ERROR;
            }
            *dst = src[i];
        }
    }

    return NGX_CONF_OK;
}


/* ============================================================
 *   Postconfiguration: register phase handlers + FastCGI hooks
 * ============================================================ */

/**
 * Register environment variable hook for downstream modules (FastCGI).
 * This is called in CONTENT_PHASE to automatically pass E vars to FastCGI.
 */
static ngx_int_t
ngx_http_apache_rewrite_env_register_hook(ngx_http_request_t *r)
{
    ngx_rewrite_ctx_t  *ctx;
    ngx_http_apache_rewrite_loc_conf_t *lcf;

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_apache_rewrite_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);

    if (ctx && ctx->env_vars && lcf && lcf->state == ENGINE_ENABLED) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite: %d environment vars available for FastCGI",
                       ctx->env_vars);
        /* Automatically add all E-vars to FastCGI params without manual config */
        ngx_http_apache_rewrite_fastcgi_register(r);

        /* Convert E=VAR:VAL format to HTTP headers for PHP/FastCGI access */
        ngx_rewrite_add_env_as_headers(r);

    }

    return NGX_DECLINED;
}

static ngx_int_t
ngx_http_apache_rewrite_url_register_hook(ngx_http_request_t *r)
{
    ngx_rewrite_ctx_t  *ctx;
    ngx_str_t          path, args;
    ngx_http_apache_rewrite_srv_conf_t *sconf;
    ngx_http_apache_rewrite_loc_conf_t *lcf;

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
    lcf = ngx_http_get_module_loc_conf(r, ngx_http_apache_rewrite_module);
    sconf = ngx_http_get_module_srv_conf(r, ngx_http_apache_rewrite_module);

    ngx_int_t is_enabled = 0;

    if (lcf && lcf->state == ENGINE_ENABLED) {
        is_enabled = 1;
    } else if (sconf && sconf->state == ENGINE_ENABLED) {
        is_enabled= 1;
    }

    if (!is_enabled) {
        return NGX_DECLINED;
    }

    if (ctx == NULL || !ctx->uri_changed) {
        if (sconf && sconf->fallback_to_index == 1) {
            return ngx_http_apache_rewrite_url_register_hook_with_fallback(r);
        } else {
            return NGX_DECLINED;
        }

    }

    if (ctx->redirect_code != 0) {
        return NGX_DECLINED;
    }

    ngx_rewrite_set_skip_after_redirect(r);

    /* Initialize path and args before calling ngx_http_split_args */
    path.data = r->uri.data;
    path.len = r->uri.len;
    args = r->args;

    if (!args.data) {
        ngx_http_split_args(r, &path, &args);
    }

    (void)ngx_http_internal_redirect(r, &path, &args);

    ngx_http_finalize_request(r, NGX_DONE);
    return NGX_DONE;
}


static ngx_int_t
ngx_http_apache_rewrite_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_handler_pt       *h;
    ngx_http_core_main_conf_t *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    /* SERVER_REWRITE_PHASE handler (runs before location matching) */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_apache_rewrite_server_handler;

    /* REWRITE_PHASE handler (runs inside matched location) */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_apache_rewrite_location_handler;

    /* CONTENT_PHASE handler - automatically pass E vars to FastCGI via HTTP headers */
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_apache_rewrite_env_register_hook;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_apache_rewrite_url_register_hook;

    return NGX_OK;
}

static ngx_int_t ngx_process_rules_result(ngx_int_t rc, ngx_rewrite_ctx_t *ctx,
    ngx_http_request_t *r, int server){
    if (rc == 0) {
        return NGX_DECLINED;
    }

    if (rc == ACTION_STATUS || rc == ACTION_STATUS_SET) {
        ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
        if (ctx && ctx->status_code > 0) {
            return ctx->status_code;
        }
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Check for redirect */
    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
    if (ctx && ctx->redirect_url.len > 0) {
        ngx_http_clear_location(r);
        r->headers_out.location = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.location == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        r->headers_out.location->hash = 1;
        ngx_str_set(&r->headers_out.location->key, "Location");
        r->headers_out.location->value = ctx->redirect_url;

        if (server){
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "mod_rewrite: redirect to \"%V\" (%d)",
                           &ctx->redirect_url, ctx->redirect_code);
        }

        return ctx->redirect_code;
    }

    if (rc == ACTION_NORMAL || rc == ACTION_NOESCAPE) {
        if (server){
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "mod_rewrite: server rewrite to \"%V\"", &r->uri);
        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "mod_rewrite: location rewrite to \"%V\"", &r->uri);
        }

        return NGX_DECLINED;
    }

    return NGX_DECLINED;
}


/* ============================================================
 *   Phase handlers
 * ============================================================ */

/*
 * Server-level rewrite handler (like Apache translate_name hook).
 * Runs in NGX_HTTP_SERVER_REWRITE_PHASE.
 */
static ngx_int_t
ngx_http_apache_rewrite_server_handler(ngx_http_request_t *r)
{
    ngx_http_apache_rewrite_srv_conf_t *sconf;
    ngx_rewrite_ctx_t                  *ctx;
    ngx_int_t                           rc;

    sconf = ngx_http_get_module_srv_conf(r, ngx_http_apache_rewrite_module);

    if (sconf == NULL || sconf->state != ENGINE_ENABLED) {
        return NGX_DECLINED;
    }

    if (sconf->rules->nelts == 0) {
        return NGX_DECLINED;
    }

    /* Check END flag */
    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
    if (ctx && ctx->end) {
        return NGX_DECLINED;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite: server handler, uri=\"%V\"", &r->uri);

    ngx_str_t baseurl = ngx_null_string;

    rc = ngx_rewrite_apply_list(r, sconf->rules, NULL, baseurl, 0);

    return ngx_process_rules_result(rc, ctx, r, 1);
}


/*
 * Create htaccess entry and store in pool cleanup (duplicated for redirect preservation)
 * Returns the created entry or NULL on error.
 */
static htaccess_entry_t *
ngx_htaccess_create_in_pool_cleanup(ngx_http_request_t *r, ngx_str_t file_path, time_t mtime,
    ngx_http_apache_rewrite_loc_conf_t *parsed_rules, ngx_htaccess_pool_cleanup_ctx_t *pool)
{
    /* Allocate pool cleanup context */
    ngx_pool_cleanup_t     *cln;
    ngx_htaccess_pool_cleanup_ctx_t *pool_cleanup_ctx;

    if (!pool){
        cln = ngx_pool_cleanup_add(r->pool, sizeof(ngx_htaccess_pool_cleanup_ctx_t));
        if (cln == NULL) {
            return NULL;
        }

        pool_cleanup_ctx = cln->data;
        pool_cleanup_ctx->htaccess_cache_entries_head = NULL;
        cln->handler = ngx_htaccess_cleanup;
    } else {
        pool_cleanup_ctx = pool;
    }

    /* Create new entry */
    htaccess_entry_t *new_entry = ngx_pcalloc(r->pool, sizeof(htaccess_entry_t));
    if (new_entry == NULL) {
        return NULL;
    }

    /* Copy file_path as string */
    new_entry->file_path = ngx_pcalloc(r->pool, file_path.len + 1);
    if (new_entry->file_path == NULL) {
        return NULL;
    }
    ngx_memcpy(new_entry->file_path, file_path.data, file_path.len);

    /* Set fallback path - use RewriteFallBack from .htaccess or default /index.php */
    if (!parsed_rules->fallback_path.data || parsed_rules->fallback_path.len == 0) {
        ngx_str_set(&new_entry->fallback_path, "/index.php");
    } else {
        new_entry->fallback_path = parsed_rules->fallback_path;
    }

    new_entry->mtime = mtime;
    new_entry->rules = parsed_rules->rules;
    new_entry->baseurl = parsed_rules->baseurl;
    new_entry->baseurl_set = parsed_rules->baseurl_set;
    new_entry->options = parsed_rules->options;
    new_entry->options_set = parsed_rules->options_set;
    new_entry->state = parsed_rules->state;
    new_entry->state_set = parsed_rules->state_set;

    /* Link into pool cleanup list */
    new_entry->next = pool_cleanup_ctx->htaccess_cache_entries_head;
    pool_cleanup_ctx->htaccess_cache_entries_head = new_entry;

    return new_entry;
}


/*
 * Cleanup handler - called when request completes or redirect occurs
 * (Currently used for documentation only - no actual cleanup needed)
 */
static void
ngx_htaccess_cleanup(void *data)
{
    /* Pool cleanup context is automatically freed with pool */
    ngx_htaccess_pool_cleanup_ctx_t *pool_cleanup_ctx = data;
    (void)pool_cleanup_ctx;
}


/*
 * Get cached htaccess entries from pool cleanup when ctx is NULL
 * Searches through r->pool->cleanup for matching handler
 */
static ngx_htaccess_pool_cleanup_ctx_t *
ngx_htaccess_get_from_pool_cleanup(ngx_http_request_t *r)
{
    ngx_pool_cleanup_t     *cln;

    /* Search through all cleanup handlers in pool */
    for (cln = r->pool->cleanup; cln != NULL; cln = cln->next) {
        if (cln->handler == ngx_htaccess_cleanup) {
            ngx_htaccess_pool_cleanup_ctx_t *pool_cleanup_ctx = cln->data;
            return pool_cleanup_ctx;
        }
    }

    /* No cached entries found */
    return NULL;
}


/*
 * Update or create htaccess cache entry in linked list
 * Uses double storage mechanism: ctx (fast access) + pool cleanup (redirect preservation)
 *
 * Algorithm:
 * 1. Try to get from ctx first
 * 2. If ctx == NULL, search in pool cleanup
 * 3. If found in cleanup - create ctx and copy entries
 * 4. If not found anywhere - create new entry in both ctx and pool cleanup
 */
static void
ngx_htaccess_update_cache(ngx_http_request_t *r, ngx_str_t file_path, time_t mtime, ngx_http_apache_rewrite_loc_conf_t *parsed_rules)
{
    ngx_rewrite_ctx_t  *ctx;
    ngx_htaccess_pool_cleanup_ctx_t   *pool_entries;

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
    pool_entries = ngx_htaccess_get_from_pool_cleanup(r);


    /* Step 1: Try to get from ctx first */
    if (ctx != NULL && ctx->htaccess_cache_head != NULL) {
        /* Search existing entry by path in ctx */
        htaccess_entry_t *entry = ctx->htaccess_cache_head;
        while (entry != NULL) {
            if (ngx_strncmp((const char *)entry->file_path, (const char *)file_path.data, file_path.len) == 0) {
                /* Found matching entry in ctx - update mtime and rules if newer */
                if (mtime >= entry->mtime) {
                    entry->mtime = mtime;
                    entry->rules = parsed_rules->rules;
                    if (!parsed_rules->fallback_path.data || parsed_rules->fallback_path.len == 0) {
                        ngx_str_set(&entry->fallback_path, "/index.php");
                    } else {
                        entry->fallback_path = parsed_rules->fallback_path;
                    }
                }
                return;
            }
            entry = entry->next;
        }

        /* Entry not found in ctx but ctx exists - need to create new */
    } else if (ctx == NULL) {

        if (pool_entries != NULL) {
            /* Found entries in pool cleanup - copy them to ctx */
            ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
            if (ctx == NULL) {
                return;
            }
            ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);

            ctx->htaccess_cache_head = pool_entries->htaccess_cache_entries_head;

            htaccess_entry_t *entry = NULL;
            if (ctx && ctx->htaccess_cache_head) {
                entry = ctx->htaccess_cache_head;
                while (entry != NULL) {
                    if (ngx_strncmp((const char *)entry->file_path, (const char *)file_path.data, file_path.len) == 0) {
                        /* Found matching entry - update mtime and rules */
                        if (mtime >= entry->mtime) {
                            entry->mtime = mtime;
                            entry->rules = parsed_rules->rules;
                            if (!parsed_rules->fallback_path.data || parsed_rules->fallback_path.len == 0) {
                                ngx_str_set(&entry->fallback_path, "/index.php");
                            } else {
                                entry->fallback_path = parsed_rules->fallback_path;
                            }
                        }
                        return; /* Entry found and updated - no need to create new */
                    }
                    entry = entry->next;
                }
            }

        }
    }

    htaccess_entry_t *new_entry = ngx_htaccess_create_in_pool_cleanup(r, file_path, mtime, parsed_rules, pool_entries);

    if (new_entry == NULL) {
        return;
    }

    /* Also add to ctx if it exists */
    if (ctx && ctx->htaccess_cache_head) {
        /* Not found in ctx - insert at head */
        ctx->htaccess_cache_head = new_entry;
    } else {
        if (ctx == NULL) {
            ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
            if (ctx == NULL) {
                return;
            }
            ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
        }
        ctx->htaccess_cache_head = new_entry;

    }
}


/*
 * Search upward from current directory to DocRoot for .htaccess file
 * Returns NGX_OK if found (found_path contains the full path)
 * Returns NGX_ERROR if not found or error occurred.
 */
static ngx_int_t
ngx_htaccess_search_upward(ngx_http_request_t *r, u_char *docroot, size_t docroot_len, ngx_str_t *current_path,
    u_char *htaccess_name, size_t htaccess_name_len, ngx_str_t *found_path)
{
    /* Start from current path (document root + URI without filename) */
    u_char *path_buf = (u_char *)ngx_palloc(r->pool, current_path->len + 1 + htaccess_name_len);
    size_t  current_pos;

    if (!path_buf) {
        return NGX_ERROR;
    }

    /* Copy current path to buffer */
    ngx_memcpy(path_buf, current_path->data, current_path->len);
    ngx_memcpy(path_buf + current_path->len , htaccess_name, htaccess_name_len);

    /* Set starting position */
    current_pos = current_path->len - 1;
    path_buf[current_path->len + htaccess_name_len] = '\0';



    /* Iterate upward from current directory up to docroot */
    while (1) {
        /* Check if file exists at this level */
        struct stat st;
        if (stat((char *)path_buf, &st) == 0 && ngx_is_file(&st)) {
            /* Found .htaccess! */
            found_path->data = path_buf;
            found_path->len = current_pos + 1 + htaccess_name_len;

            return NGX_OK;
        }

        /* Go up one level: remove last directory component */
        u_char *last_slash = NULL;
        for (u_char *p = path_buf + current_pos - 2; p >= path_buf && docroot_len <= (size_t)(p - (u_char *)&path_buf); p--) {
            if (*p == '/') {
                last_slash = p;
                break;
            }
        }

        if (last_slash == NULL) {
            /* Reached DocRoot without finding htaccess */
            return NGX_ERROR;
        }

        /* Remove the directory component */
        current_pos = last_slash - path_buf;

        if (current_pos < docroot_len) {
            /* Reached or passed DocRoot - no htaccess found */
            return NGX_ERROR;
        }

        ngx_memcpy(path_buf + current_pos + 1, htaccess_name, htaccess_name_len);
        path_buf[current_pos + 1 + htaccess_name_len] = '\0';
    }
}

/*
 * Build .htaccess path for current request.
 * Searches upward from current directory up to DocRoot.
 * Returns NGX_OK if found, NGX_ERROR if not found or error occurred.
 */
static ngx_int_t
ngx_htaccess_build_path(ngx_http_request_t *r, u_char *docroot, size_t docroot_len,
    ngx_str_t *path, u_char *htaccess_name, size_t htaccess_len)
{
    /* Build full path: docroot + r->uri */
    size_t uri_len = r->uri.len;

    /* Calculate exact size needed: docroot + '/' + uri + .htaccess */
    /* Add extra for safety */
    size_t alloc_size = docroot_len + 1 + uri_len + htaccess_len * 2;

    path->data = (u_char *)ngx_palloc(r->pool, alloc_size);
    if (path->data == NULL) {
        return NGX_ERROR;
    }

    /* Copy docroot */
    ngx_memcpy(path->data, docroot, docroot_len);
    path->len = docroot_len;

    if (path->len > 0 && path->data[path->len - 1] != '/') {
        path->data[path->len++] = '/';
    }

    /* Add separator '/' between docroot and URI */
    if (uri_len > 0) {
        if (r->uri.data[0] != '/') {
            ngx_memcpy(path->data + path->len, r->uri.data, uri_len);
            path->len += uri_len;
        } else {
            ngx_memcpy(path->data + path->len, r->uri.data + 1, uri_len - 1);
            path->len += (uri_len - 1);
        }

        /* Find last '/' in path and extract directory component */
        u_char *last_slash = NULL;
        for (u_char *p = path->data + path->len - 1; p >= path->data; p--) {
            if (*p == '/') {
                last_slash = p;
                break;
            }
        }

        if (last_slash) {
            /* Found directory in URI - truncate to include only directory */
            /* Keep everything up to and including the last '/' */
            path->len = last_slash - path->data + 1;

        } else {
            //Nothing do
        }

    } else {
        ngx_memcpy(path->data + path->len, htaccess_name, htaccess_len);
        path->len += htaccess_len;
    }

    return NGX_OK;
}


static ngx_array_t *
ngx_htaccess_parse_line(ngx_pool_t *pool, const char *line)
{
    /* Allocate a mutable copy of the input line */
    size_t  len   = strlen(line);
    u_char *buf   = ngx_palloc(pool, len + 1);
    u_char *b_word = NULL;
    if (buf == NULL) {
        return NULL;
    }
    ngx_memcpy(buf, line, len + 1);

    /* Trim leading whitespace */
    u_char *start = buf;
    while (*start && isspace(*start)) {
        start++;
    }

    /* Trim trailing whitespace */
    u_char *end = buf + strlen((char *)start);
    while (end > start && isspace(end[-1])) {
        end--;
    }
    size_t trimmed_len = end - start;
    if (trimmed_len == 0) {
        return NULL;   /* empty line after trimming */
    }

    /* If the first non‑whitespace character is '#', ignore the line */
    if (*start == '#') {
        return NULL;
    }

    /* Create an array for the words */
    ngx_array_t *words = ngx_array_create(pool, 4, sizeof(ngx_str_t));
    if (words == NULL) {
        return NULL;
    }

    /* Split on whitespace, collapsing multiple spaces */
    u_char *p = start;
    while (p < end) {
        /* Skip any leading spaces between words */
        while (p < end && isspace(*p)) {
            p++;
        }
        if (p >= end) {
            break;
        }

        u_char *w_start = p;
        while (p < end && !isspace(*p)) {
            p++;
        }
        size_t w_len = p - w_start;

        /* Handle double-quoted strings: strip surrounding quotes */
        if (w_len >= 2 && w_start[0] == '"' && w_start[w_len - 1] == '"') {
            w_start += 1;
            w_len -= 2;
        }

        ngx_str_t *word = ngx_array_push(words);
        if (word == NULL) {
            return NULL;
        }

        b_word   = ngx_palloc(pool, w_len + 1);
        if (!b_word) {
            return NULL;
        }

        ngx_memzero(b_word, w_len + 1);
        ngx_memcpy(b_word, w_start, w_len);

        word->data = b_word;
        word->len  = w_len;
    }

    return words;
}

static void * ngx_http_rewrite_rule_helper(int parsing, ngx_http_apache_rewrite_srv_conf_t *sconf,
    ngx_http_apache_rewrite_loc_conf_t *lcf, ngx_conf_t *cf, ngx_array_t *args, ngx_http_request_t *r){
    ngx_str_t                          *value;
    ngx_rewrite_rule_t                 *rule;
    ngx_regex_compile_t                 rc;
    u_char                              errstr[NGX_MAX_CONF_ERRSTR];
    ngx_array_t                        *rules, *pending_conds;
    ngx_uint_t                          i;
    ngx_rewrite_cond_t                 *pending, *cond_dst;
    ngx_http_apache_rewrite_loc_conf_t *conf;
    ngx_pool_t *pool;
    ngx_uint_t nelts = 0;

    if (!parsing) {
        value = cf->args->elts;
        nelts = cf->args->nelts;

        /* Determine if we're in server or location context */
        if (cf->cmd_type & NGX_HTTP_LOC_CONF) {
            rules = lcf->rules;
            pending_conds = lcf->pending_conds;
        } else {
            rules = sconf->rules;
            pending_conds = sconf->pending_conds;
        }
        pool = cf->pool;
    } else {
        conf = lcf;
        value = args->elts;
        nelts = args->nelts;

        rules = conf->rules;
        pending_conds = conf->pending_conds;
        pool = r->pool;
    }

    /* Create new rule */
    rule = ngx_array_push(rules);
    if (rule == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(rule, sizeof(ngx_rewrite_rule_t));

    rule->maxrounds = NGX_REWRITE_MAX_ROUNDS;
    rule->forced_responsecode = 0;

    /* arg1: pattern */
    rule->pattern = value[1];

    /* Check for negation prefix '!' */
    if (rule->pattern.len > 0 && rule->pattern.data[0] == '!') {
        rule->flags |= RULEFLAG_NOTMATCH;
        rule->pattern.data++;
        rule->pattern.len--;
    }

    /* arg3: optional flags */
    if (nelts >= 4) {
        ngx_str_t flags_str = value[3];
        ngx_rewrite_strip_brackets(&flags_str);
        if (ngx_http_rewrite_parse_rule_flags(pool, &flags_str, rule)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    /* Compile regex (after flags parsed, to know NC) */
    if (value[2].len == 1 && value[2].data[0] == '-') {
        /* Substitution '-' = no substitution */
        rule->flags |= RULEFLAG_NOSUB;
        ngx_str_null(&rule->output);
        /* Still need to compile the pattern for matching */
    } else {
        rule->output = value[2];
    }

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
    rc.pattern = rule->pattern;
    rc.pool = pool;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    if (rule->flags & RULEFLAG_NOCASE) {
        rc.options = NGX_REGEX_CASELESS;
    }

    if (ngx_regex_compile(&rc) != NGX_OK) {
        if (!parsing) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "RewriteRule: invalid regex \"%V\": %V",
                &rule->pattern, &rc.err);
        } else {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                "RewriteRule: invalid regex \"%V\": %V",
                &rule->pattern, &rc.err);
        }
        return NGX_CONF_ERROR;
    }

    rule->regex = rc.regex;

    /* Attach pending conditions */
    rule->conditions = ngx_array_create(pool, pending_conds->nelts + 1,
                                        sizeof(ngx_rewrite_cond_t));
    if (rule->conditions == NULL) {
        return NGX_CONF_ERROR;
    }

    if (pending_conds->nelts > 0) {
        pending = pending_conds->elts;
        for (i = 0; i < pending_conds->nelts; i++) {
            cond_dst = ngx_array_push(rule->conditions);
            if (cond_dst == NULL) {
                return NGX_CONF_ERROR;
            }
            *cond_dst = pending[i];
        }
        pending_conds->nelts = 0; /* clear pending */
    }

    return NULL;

}

void * ngx_http_rewrite_cond_helper(int parsing, ngx_http_apache_rewrite_srv_conf_t *sconf,
    ngx_http_apache_rewrite_loc_conf_t *lcf, ngx_conf_t *cf, ngx_array_t *args, ngx_http_request_t *r){
        ngx_str_t                          *value;
        ngx_rewrite_cond_t                 *cond;
        ngx_array_t                        *pending_conds;
        ngx_regex_compile_t                 rc;
        u_char                              errstr[NGX_MAX_CONF_ERRSTR];
        u_char                             *pat;
        size_t                              pat_len;
        ngx_pool_t *pool;
        ngx_uint_t nelts = 0;
        ngx_http_apache_rewrite_loc_conf_t *conf;

        if (!parsing){
            pool = cf->pool;
            nelts = cf->args->nelts;
            value = cf->args->elts;

            sconf = ngx_http_conf_get_module_srv_conf(cf,
                        ngx_http_apache_rewrite_module);
            lcf = ngx_http_conf_get_module_loc_conf(cf,
                        ngx_http_apache_rewrite_module);

            /* Determine context */
            if (cf->cmd_type & NGX_HTTP_LOC_CONF) {
                pending_conds = lcf->pending_conds;
            } else {
                pending_conds = sconf->pending_conds;
            }
        } else {
            conf = lcf;
            value = args->elts;
            nelts = args->nelts;

            pending_conds = conf->pending_conds;
            pool = r->pool;
        }

        /* Create new condition in pending list */
        cond = ngx_array_push(pending_conds);
        if (cond == NULL) {
            return NGX_CONF_ERROR;
        }
        ngx_memzero(cond, sizeof(ngx_rewrite_cond_t));

        /* arg1: test string */
        cond->input = value[1];

        /* arg3: optional flags (parse before pattern for NC) */
        cond->flags = CONDFLAG_NONE;
        if (nelts >= 4) {
            ngx_str_t flags_str = value[3];
            ngx_rewrite_strip_brackets(&flags_str);
            if (ngx_http_rewrite_parse_cond_flags(cf, &flags_str, cond)
                != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }
        }

        /* arg2: condition pattern */
        pat = value[2].data;
        pat_len = value[2].len;

        /* Check for negation */
        if (pat_len > 0 && pat[0] == '!') {
            cond->flags |= CONDFLAG_NOTMATCH;
            pat++;
            pat_len--;
        }

        /* Determine pattern type */
        cond->ptype = CONDPAT_REGEX;

        if (pat_len >= 2 && pat[0] == '-') {
            /* File/directory test patterns */
            if (pat_len == 2) {
                switch (pat[1]) {
                case 'f': cond->ptype = CONDPAT_FILE_EXISTS; break;
                case 's': cond->ptype = CONDPAT_FILE_SIZE;   break;
                case 'd': cond->ptype = CONDPAT_FILE_DIR;    break;
                case 'x': cond->ptype = CONDPAT_FILE_XBIT;   break;
                case 'h': case 'l': case 'L':
                    cond->ptype = CONDPAT_FILE_LINK;
                    break;
                }
            }
            /* Integer comparison: -lt, -le, -eq, -gt, -ge, -ne */
            else if (pat_len >= 3 && pat[1] != '-') {
                if (pat[1] == 'l' && pat[2] == 't') {
                    cond->ptype = CONDPAT_INT_LT;
                    pat += 3; pat_len -= 3;
                } else if (pat[1] == 'l' && pat[2] == 'e') {
                    cond->ptype = CONDPAT_INT_LE;
                    pat += 3; pat_len -= 3;
                } else if (pat[1] == 'e' && pat[2] == 'q') {
                    cond->ptype = CONDPAT_INT_EQ;
                    pat += 3; pat_len -= 3;
                } else if (pat[1] == 'g' && pat[2] == 't') {
                    cond->ptype = CONDPAT_INT_GT;
                    pat += 3; pat_len -= 3;
                } else if (pat[1] == 'g' && pat[2] == 'e') {
                    cond->ptype = CONDPAT_INT_GE;
                    pat += 3; pat_len -= 3;
                } else if (pat[1] == 'n' && pat[2] == 'e') {
                    /* -ne is !-eq */
                    cond->ptype = CONDPAT_INT_EQ;
                    cond->flags ^= CONDFLAG_NOTMATCH;
                    pat += 3; pat_len -= 3;
                }
            }
        }
        /* String comparison operators */
        else if (pat_len >= 1) {
            if (pat[0] == '>') {
                if (pat_len >= 2 && pat[1] == '=') {
                    cond->ptype = CONDPAT_STR_GE;
                    pat += 2; pat_len -= 2;
                } else {
                    cond->ptype = CONDPAT_STR_GT;
                    pat += 1; pat_len -= 1;
                }
            } else if (pat[0] == '<') {
                if (pat_len >= 2 && pat[1] == '=') {
                    cond->ptype = CONDPAT_STR_LE;
                    pat += 2; pat_len -= 2;
                } else {
                    cond->ptype = CONDPAT_STR_LT;
                    pat += 1; pat_len -= 1;
                }
            } else if (pat[0] == '=') {
                cond->ptype = CONDPAT_STR_EQ;
                pat += 1; pat_len -= 1;
                /* Handle ="" for empty string */
                if (pat_len == 2 && pat[0] == '"' && pat[1] == '"') {
                    pat_len = 0;
                }
            }
        }

        /* Store the (possibly adjusted) pattern */
        cond->pattern.data = pat;
        cond->pattern.len = pat_len;

        /* Compile regex if needed */
        if (cond->ptype == CONDPAT_REGEX) {
            ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
            rc.pattern.data = pat;
            rc.pattern.len = pat_len;
            rc.pool = pool;
            rc.err.len = NGX_MAX_CONF_ERRSTR;
            rc.err.data = errstr;

            if (cond->flags & CONDFLAG_NOCASE) {
                rc.options = NGX_REGEX_CASELESS;
            }

            if (ngx_regex_compile(&rc) != NGX_OK) {
                if (!parsing){
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                        "RewriteCond: invalid regex \"%*s\": %V",
                        pat_len, pat, &rc.err);
                } else {
                    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                        "RewriteCond: invalid regex \"%*s\": %V",
                        pat_len, pat, &rc.err);
                }
                return NGX_CONF_ERROR;
            }

            cond->regex = rc.regex;
        }

        return NULL;
}

/*
 * Parse .htaccess file and extract rewrite rules.
 * Returns NGX_OK on success, NGX_ERROR on error.
 */
static ngx_http_apache_rewrite_loc_conf_t *
ngx_htaccess_parse_file_from_ha(ngx_http_request_t *r, ngx_str_t *file_path)
{
    FILE   *fp;
    char  line_buf_raw[4096];
    char  line_buf[4096];
    size_t  line_len;

    ngx_http_apache_rewrite_loc_conf_t *conf;
    ngx_pool_t *pool = r->pool;

    conf = ngx_pcalloc(pool, sizeof(ngx_http_apache_rewrite_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->state = ENGINE_DISABLED;
    conf->options = OPTION_NONE;

    conf->rules = ngx_array_create(pool, 4, sizeof(ngx_rewrite_rule_t));
    conf->pending_conds = ngx_array_create(pool, 4,
                                           sizeof(ngx_rewrite_cond_t));

    if (conf->rules == NULL || conf->pending_conds == NULL) {
        return NULL;
    }

    ngx_str_null(&conf->baseurl);
    ngx_str_null(&conf->fallback_path);

    fp = fopen((const char *)file_path->data, "r");
    if (fp == NULL) {
        return NULL;
    }
    /* Initialize state to enabled */
    while (fgets(line_buf_raw, sizeof(line_buf_raw), fp) != NULL) {
        line_len = strlen(line_buf_raw);

        char *p_line_buf_raw = line_buf_raw;
        /* Skip leading spaces and tabs */
        while (*p_line_buf_raw && (*p_line_buf_raw == ' ' || *p_line_buf_raw == '\t')) {
            p_line_buf_raw++;
        }
        /* Skip trailing spaces and tabs */
        char *q_line_buf_raw = p_line_buf_raw + strlen(p_line_buf_raw) - 1;
        while (q_line_buf_raw >= p_line_buf_raw && (*q_line_buf_raw == ' ' || *q_line_buf_raw == '\t' || *q_line_buf_raw == '\n')) {
            *q_line_buf_raw = '\0';
            q_line_buf_raw--;
        }
        size_t trimmed_len = strlen(p_line_buf_raw);
        ngx_memcpy(line_buf, p_line_buf_raw, trimmed_len + 1); /* include null terminator */
        line_len = trimmed_len;

        /* Skip empty lines and comments */
        if (line_len == 0 || line_buf[0] == '#' || line_buf[0] == '\n') {
            continue;
        }
        /* Trim trailing newline */
        if (line_len > 0 && line_buf[line_len - 1] == '\n') {
            line_buf[line_len - 1] = '\0';
            line_len--;
        }
        /* -----------------------------------------------------------------
         *  RewriteEngine
         * ----------------------------------------------------------------- */
        if (ngx_strncmp((u_char *)line_buf, "RewriteEngine", 13) == 0) {
            u_char *p = (u_char *)(line_buf + 13);
            while (*p && *p == ' ') p++;
            if (ngx_strncasecmp(p, (u_char *)"on", 2) == 0) {
                conf->state = ENGINE_ENABLED;
            } else if (ngx_strncasecmp(p, (u_char *)"off", 3) == 0) {
                conf->state = ENGINE_DISABLED;
            }
        /* -----------------------------------------------------------------
         *  RewriteRule
         * ----------------------------------------------------------------- */
        } else if (ngx_strncmp(line_buf, "RewriteRule", 11) == 0) {
            ngx_array_t *args = ngx_htaccess_parse_line(pool, (const char *)line_buf);

            if (args) {

                void *result = ngx_http_rewrite_rule_helper(1, NULL,
                    conf, NULL, args, r);

                if (result == NGX_CONF_ERROR) {
                    fclose(fp);
                    return NULL;
                }

            }

        /* -----------------------------------------------------------------
         *  RewriteBase – ignored in .htaccess
         * ----------------------------------------------------------------- */
        } else if (ngx_strncmp(line_buf, "RewriteBase", 11) == 0) {

            ngx_array_t *args = ngx_htaccess_parse_line(pool, (const char *)line_buf);

            if (args) {
                ngx_http_apache_rewrite_loc_conf_t *lcf = conf;
                ngx_str_t                          *value;

                value = args->elts;

                if (value[1].len == 0 || value[1].data[0] != '/') {
                    return NULL;
                }

                lcf->baseurl = value[1];
                lcf->baseurl_set = 1;
            }

        /* -----------------------------------------------------------------
         *  RewriteCond
         * ----------------------------------------------------------------- */
        } else if (ngx_strncmp(line_buf, "RewriteCond", 11) == 0) {

            ngx_array_t *args = ngx_htaccess_parse_line(pool, (const char *)line_buf);

            if (args) {

                void *result = ngx_http_rewrite_cond_helper(1, NULL,
                    conf, NULL, args, r);

                if (result == NGX_CONF_ERROR) {
                    fclose(fp);
                    return NULL;
                }

            }


        /* -----------------------------------------------------------------
         *  RewriteOptions – ignored in .htaccess
         * ----------------------------------------------------------------- */
        } else if (ngx_strncmp(line_buf, "RewriteOptions", 14) == 0) {

            ngx_array_t *args = ngx_htaccess_parse_line(pool, (const char *)line_buf);

            if (args) {
                ngx_str_t                          *value;
                ngx_uint_t                          i;

                value = args->elts;

                for (i = 1; i < args->nelts; i++) {
                    if (ngx_strcasecmp(value[i].data, (u_char *) "Inherit") == 0) {
                        conf->options |= OPTION_INHERIT;
                        conf->options_set = 1;
                    } else if (ngx_strcasecmp(value[i].data,
                               (u_char *) "InheritBefore") == 0)
                    {
                        conf->options |= OPTION_INHERIT | OPTION_INHERIT_BEFORE;
                        conf->options_set = 1;

                    }
                }
            }

        /* -----------------------------------------------------------------
         *  RewriteFallBack – alternative fallback path (override default /index.php)
         * ----------------------------------------------------------------- */
        } else if (ngx_strncmp(line_buf, "RewriteFallBack", 15) == 0) {

            ngx_array_t *args = ngx_htaccess_parse_line(pool, (const char *)line_buf);

            if (args && args->nelts >= 2) {
                ngx_str_t                          *value;

                value = args->elts;

                /* Validate fallback path starts with '/' */
                if (value[1].len > 0 && value[1].data[0] != '/') {
                    return NULL;
                }

                conf->fallback_path = value[1];
            }

        }
    }
    fclose(fp);
    return conf;
}


/*
 * Location-level rewrite handler (like Apache fixups hook).
 * Runs in NGX_HTTP_REWRITE_PHASE.
 */
static ngx_int_t
ngx_http_apache_rewrite_location_handler(ngx_http_request_t *r)
{
    ngx_http_apache_rewrite_loc_conf_t *lcf;
    ngx_http_apache_rewrite_srv_conf_t *sconf;
    ngx_http_core_loc_conf_t           *clcf;
    ngx_rewrite_ctx_t                  *ctx;
    ngx_str_t                           htaccess_path = ngx_null_string;
    ngx_int_t                           rc;
    ngx_http_apache_rewrite_loc_conf_t  *parsed_rules;
    ngx_array_t                         *combined_rules;
    ngx_str_t baseurl_htacess = ngx_null_string;
    ngx_int_t options_htaccess = -1;
    ngx_int_t state_htaccess = -1;

    (void)options_htaccess;

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_apache_rewrite_module);
    sconf = ngx_http_get_module_srv_conf(r, ngx_http_apache_rewrite_module);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (lcf == NULL || lcf->state != ENGINE_ENABLED) {
        return NGX_DECLINED;
    }

    /* Check htaccess parsing enable */
    if (!sconf->htaccess_enable_set || sconf->htaccess_enable != 1) {
        /* Use only location rules */
        if (lcf->rules->nelts == 0) {
            return NGX_DECLINED;
        }

        ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
        if (ctx == NULL) {
            ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
            if (ctx == NULL) {
                return NGX_ERROR;
            }
            ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
        }

        if (ctx && ctx->end) {
            return NGX_DECLINED;
        }

        /* Set default fallback path from config */
        if (!ctx || !ctx->fallback_path.data) {
            ngx_str_set(&ctx->fallback_path, "/index.php");
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite: location handler (no htaccess), uri=\"%V\"", &r->uri);

        rc = ngx_rewrite_apply_list(r, lcf->rules, NULL, lcf->baseurl, lcf->baseurl_set);

        return ngx_process_rules_result(rc, ctx, r, 0);
    }

    /* Search upward from current directory up to DocRoot for .htaccess */
    u_char *htaccess_name = sconf->htaccess_name.data ? (u_char *)sconf->htaccess_name.data : (u_char *)".htaccess";
    size_t htaccess_name_len = sconf->htaccess_name.data ? sconf->htaccess_name.len : 9;

    /* Build initial path: docroot + r->uri */
    ngx_str_t current_path;
    rc = ngx_htaccess_build_path(r, clcf->root.data, clcf->root.len, &current_path, htaccess_name, htaccess_name_len);

    ngx_str_t htaccess_docroot = ngx_null_string;

    /* Remove last component (filename) to search from parent directory */
    if (rc == NGX_OK && current_path.len > 0) {

        /* Search upward for .htaccess file */
        rc = ngx_htaccess_search_upward(r, clcf->root.data, clcf->root.len, &current_path,
                                        htaccess_name, htaccess_name_len, &htaccess_path);

        if (rc == NGX_ERROR) {
                /* File not found — use only location rules */
                ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
                if (ctx && ctx->end) {
                    return NGX_DECLINED;
                }

                rc = ngx_rewrite_apply_list(r, lcf->rules, NULL, lcf->baseurl, lcf->baseurl_set);

                return ngx_process_rules_result(rc, ctx, r, 0);
        }

        htaccess_docroot.data = ngx_pcalloc(r->pool, htaccess_path.len);
        size_t htaccess_docroot_vlen = htaccess_path.len - (clcf->root.len + 1) - htaccess_name_len;
        if ((long)htaccess_docroot_vlen < 0) htaccess_docroot_vlen = 0;
        ngx_memcpy(htaccess_docroot.data, htaccess_path.data + clcf->root.len + 1, htaccess_docroot_vlen);

        htaccess_docroot.len = htaccess_path.len - (clcf->root.len + 1) - htaccess_name_len;
    }

    /* Check if htaccess file exists */
    struct stat st;
    if (!htaccess_path.data || stat((char *)htaccess_path.data, &st) == -1) {
        /* File does not exist — use only location rules */
        if (lcf->rules->nelts == 0) {
            return NGX_DECLINED;
        }

        ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
        if (ctx == NULL) {
            ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
            if (ctx == NULL) {
                return NGX_ERROR;
            }
            ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
        }

        if (ctx && ctx->end) {
            return NGX_DECLINED;
        }

        /* Set default fallback path */
        if (!ctx || !ctx->fallback_path.data) {
            ngx_str_set(&ctx->fallback_path, "/index.php");
        }

        rc = ngx_rewrite_apply_list(r, lcf->rules, NULL, lcf->baseurl, lcf->baseurl_set);

        return ngx_process_rules_result(rc, ctx, r, 0);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);

    /* Step 1: Try to get cached entries from ctx first */
    if (ctx == NULL) {
        /* Step 2: No ctx, search in pool cleanup for preserved htaccess cache */
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
        if (ctx != NULL) {
            /* Copy all cached entries from pool cleanup into ctx */
            ngx_htaccess_pool_cleanup_ctx_t *pool_entries = ngx_htaccess_get_from_pool_cleanup(r);
            if (pool_entries != NULL) {
                ctx->htaccess_cache_head = pool_entries->htaccess_cache_entries_head;

                /* Set ctx for request */
                ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
            }
        }
    }

    /* Search by linked list — find entry matching file_path */
    combined_rules = NULL;
    ngx_str_t fallback_from_cache = ngx_null_string;
    if (ctx && ctx->htaccess_cache_head) {
        htaccess_entry_t *entry = ctx->htaccess_cache_head;
        while (entry != NULL) {
            if (ngx_strncmp((const char *)entry->file_path, (const char *)htaccess_path.data, htaccess_path.len) == 0 &&
                entry->mtime <= st.st_mtime) {
                /* Found matching cached entry — use it */
                combined_rules = entry->rules;
                fallback_from_cache = entry->fallback_path;
                if (entry->state_set)
                    state_htaccess = entry->state;
                if (entry->options_set)
                    options_htaccess = entry->options;
                if (entry->baseurl_set)
                    baseurl_htacess = entry->baseurl;
                break;
            }
            entry = entry->next;
        }
    }

    /* If no cached rules, parse .htaccess file */
    if (combined_rules == NULL) {

        parsed_rules = ngx_htaccess_parse_file_from_ha(r, &htaccess_path);

        if (!parsed_rules){
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        combined_rules = parsed_rules->rules;
        fallback_from_cache = !parsed_rules->fallback_path.data || parsed_rules->fallback_path.len == 0 ?
                              (ngx_str_t)ngx_null_string : parsed_rules->fallback_path;
        if (parsed_rules->state_set)
            state_htaccess = parsed_rules->state;
        if (parsed_rules->options_set)
            options_htaccess = parsed_rules->options;
        if (parsed_rules->baseurl_set)
            baseurl_htacess = parsed_rules->baseurl;

        /* Store in cache — add/update entry in linked list */
        ngx_htaccess_update_cache(r, htaccess_path, st.st_mtime, parsed_rules);

    }

    if (state_htaccess == ENGINE_DISABLED)
        return NGX_DECLINED;

    /* Ensure ctx exists */
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
    }

    /* Set fallback path from htaccess cache or use default */
    if (!ctx->fallback_path.data) {
        if (fallback_from_cache.len > 0) {
            ctx->fallback_path = fallback_from_cache;
        } else {
            ngx_str_set(&ctx->fallback_path, "/index.php");
        }
    }

    /* Combine location rules with .htaccess rules (.htaccess has priority - added first) */
    ngx_int_t final_rc;

    if (combined_rules->nelts > 0 && lcf->rules->nelts > 0) {
        /* Create combined array: htaccess rules first, then location rules */
        ngx_array_t *final_rules = ngx_array_create(r->pool,
            combined_rules->nelts + lcf->rules->nelts, sizeof(ngx_rewrite_rule_t));

        if (final_rules == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        /* Copy htaccess rules first */
        for (ngx_uint_t j = 0; j < combined_rules->nelts; j++) {
            ngx_rewrite_rule_t *src, *dst;
            src = (ngx_rewrite_rule_t *)combined_rules->elts + j;
            dst = ngx_array_push(final_rules);
            if (dst == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            *dst = *src;
        }

        /* Copy location rules after */
        for (ngx_uint_t j = 0; j < lcf->rules->nelts; j++) {
            ngx_rewrite_rule_t *src, *dst;
            src = (ngx_rewrite_rule_t *)lcf->rules->elts + j;
            dst = ngx_array_push(final_rules);
            if (dst == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            *dst = *src;
        }

        ngx_str_t local_base_url = ngx_null_string;
        if (baseurl_htacess.len>0)
            local_base_url = baseurl_htacess;
        else if (lcf->baseurl_set)
            local_base_url = lcf->baseurl;

        final_rc = ngx_rewrite_apply_list(r, final_rules, &htaccess_docroot, local_base_url, local_base_url.len>0?1:0);

    } else if (combined_rules->nelts > 0) {
        /* Only htaccess rules */
        final_rc = ngx_rewrite_apply_list(r, combined_rules, &htaccess_docroot, baseurl_htacess, baseurl_htacess.len>0?1:0);

    } else {
        /* Only location rules */
        final_rc = ngx_rewrite_apply_list(r, lcf->rules, NULL, lcf->baseurl, lcf->baseurl_set);
    }

    return ngx_process_rules_result(final_rc, ctx, r, 0);
}


/* ============================================================
 *   Flag parsing
 * ============================================================ */

/*
 * Parse RewriteRule flags: "[R=301,L,NC,QSA]"
 * Expects the content between [ and ] (without brackets).
 */
static ngx_int_t
ngx_http_rewrite_parse_rule_flags(ngx_pool_t *pool,
    ngx_str_t *flags_str, ngx_rewrite_rule_t *rule)
{
    u_char  *p, *start, *end, *eq;
    u_char   flag_buf[256];
    u_char   val_buf[256];
    size_t   flag_len, val_len;

    if (flags_str->len == 0) {
        return NGX_OK;
    }

    p = flags_str->data;
    end = p + flags_str->len;

    while (p < end) {
        /* skip whitespace and commas */
        while (p < end && (*p == ',' || *p == ' ' || *p == '\t')) {
            p++;
        }
        if (p >= end) {
            break;
        }

        start = p;

        /* Find the end of this flag (comma or end) */
        while (p < end && *p != ',') {
            p++;
        }

        /* Extract flag=value */
        eq = ngx_strlchr(start, p, '=');
        if (eq) {
            flag_len = eq - start;
            val_len = p - eq - 1;
            if (flag_len >= sizeof(flag_buf)) flag_len = sizeof(flag_buf) - 1;
            if (val_len >= sizeof(val_buf)) val_len = sizeof(val_buf) - 1;
            ngx_memcpy(flag_buf, start, flag_len);
            flag_buf[flag_len] = '\0';
            ngx_memcpy(val_buf, eq + 1, val_len);
            val_buf[val_len] = '\0';
        } else {
            flag_len = p - start;
            val_len = 0;
            if (flag_len >= sizeof(flag_buf)) flag_len = sizeof(flag_buf) - 1;
            ngx_memcpy(flag_buf, start, flag_len);
            flag_buf[flag_len] = '\0';
            val_buf[0] = '\0';
        }

        /* Match flag names */
        switch (flag_buf[0]) {

        case 'B': case 'b':
            if (flag_len == 1) {
                rule->flags |= RULEFLAG_ESCAPEBACKREF;
            }
            break;

        case 'C': case 'c':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"chain",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_CHAIN;
            } else if (flag_len == 2
                       && (flag_buf[1] == 'O' || flag_buf[1] == 'o'))
            {
                /* CO=cookie — Phase 2 */
            }
            break;

        case 'D': case 'd':
            if (flag_len == 3
                && ngx_strncasecmp(flag_buf, (u_char *)"DPI", 3) == 0)
            {
                rule->flags |= RULEFLAG_DISCARDPATHINFO;
            }
            break;

        case 'E': case 'e':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"env", 3) == 0)
            {
                /* E=VAR:VAL — add to env list */
                if (val_len > 0) {
                    ngx_rewrite_data_item_t *item;
                    item = ngx_palloc(pool,
                                      sizeof(ngx_rewrite_data_item_t));
                    if (item == NULL) {
                        return NGX_ERROR;
                    }
                    item->data.data = ngx_pnalloc(pool, val_len);
                    if (item->data.data == NULL) {
                        return NGX_ERROR;
                    }
                    ngx_memcpy(item->data.data, val_buf, val_len);
                    item->data.len = val_len;
                    item->next = rule->env;
                    rule->env = item;
                }
            } else if (flag_len == 3
                       && ngx_strncasecmp(flag_buf, (u_char *)"END",
                                          3) == 0)
            {
                rule->flags |= RULEFLAG_END;
            }
            break;

        case 'F': case 'f':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"forbidden",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_STATUS | RULEFLAG_NOSUB;
                rule->forced_responsecode = NGX_HTTP_FORBIDDEN;
            }
            break;

        case 'G': case 'g':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"gone",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_STATUS | RULEFLAG_NOSUB;
                rule->forced_responsecode = 410;
            }
            break;

        case 'L': case 'l':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"last",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_LASTRULE;
            }
            break;

        case 'N': case 'n':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"next",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_NEWROUND;
                if (val_len > 0) {
                    rule->maxrounds = ngx_atoi(val_buf, val_len);
                    if (rule->maxrounds == NGX_ERROR) {
                        rule->maxrounds = NGX_REWRITE_MAX_ROUNDS;
                    }
                }
            } else if (flag_len == 2
                       && (flag_buf[1] == 'C' || flag_buf[1] == 'c'))
            {
                rule->flags |= RULEFLAG_NOCASE;
            } else if (flag_len == 2
                       && (flag_buf[1] == 'E' || flag_buf[1] == 'e'))
            {
                rule->flags |= RULEFLAG_NOESCAPE;
            } else if (flag_len == 2
                       && (flag_buf[1] == 'S' || flag_buf[1] == 's'))
            {
                rule->flags |= RULEFLAG_IGNOREONSUBREQ;
            }
            break;

        case 'P': case 'p':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"proxy",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_PROXY;
            } else if (flag_len == 2
                       && (flag_buf[1] == 'T' || flag_buf[1] == 't'))
            {
                rule->flags |= RULEFLAG_PASSTHROUGH;
            }
            break;

        case 'Q': case 'q':
            if (flag_len == 3
                && ngx_strncasecmp(flag_buf, (u_char *)"QSA", 3) == 0)
            {
                rule->flags |= RULEFLAG_QSAPPEND;
            } else if (flag_len == 3
                       && ngx_strncasecmp(flag_buf, (u_char *)"QSD",
                                          3) == 0)
            {
                rule->flags |= RULEFLAG_QSDISCARD;
            } else if (flag_len == 3
                       && ngx_strncasecmp(flag_buf, (u_char *)"QSL",
                                          3) == 0)
            {
                rule->flags |= RULEFLAG_QSLAST;
            }
            break;

        case 'R': case 'r':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"redirect",
                                   flag_len) == 0)
            {
                rule->flags |= RULEFLAG_FORCEREDIRECT;
                if (val_len > 0) {
                    if (ngx_strncasecmp(val_buf,
                        (u_char *)"permanent", val_len) == 0)
                    {
                        rule->forced_responsecode =
                            NGX_HTTP_MOVED_PERMANENTLY;
                    } else if (ngx_strncasecmp(val_buf,
                               (u_char *)"temp", val_len) == 0)
                    {
                        rule->forced_responsecode =
                            NGX_HTTP_MOVED_TEMPORARILY;
                    } else if (ngx_strncasecmp(val_buf,
                               (u_char *)"seeother", val_len) == 0)
                    {
                        rule->forced_responsecode = NGX_HTTP_SEE_OTHER;
                    } else {
                        ngx_int_t code = ngx_atoi(val_buf, val_len);
                        if (code >= 100 && code < 600) {
                            rule->forced_responsecode = code;
                            /* Non-redirect codes: set status+nosub */
                            if (code < 300 || code >= 400) {
                                rule->flags |= RULEFLAG_STATUS
                                              | RULEFLAG_NOSUB;
                            }
                        } else {
                            ngx_log_error(NGX_LOG_WARN, pool->log, 0,
                                "mod_rewrite: invalid redirect code "
                                "\"%*s\"", val_len, val_buf);
                            return NGX_ERROR;
                        }
                    }
                }
                /* Default redirect code */
                if (rule->forced_responsecode == 0) {
                    rule->forced_responsecode =
                        NGX_HTTP_MOVED_TEMPORARILY;
                }
            }
            break;

        case 'S': case 's':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"skip",
                                   flag_len) == 0)
            {
                if (val_len > 0) {
                    rule->skip = ngx_atoi(val_buf, val_len);
                    if (rule->skip == NGX_ERROR) {
                        rule->skip = 0;
                    }
                }
            }
            break;

        case 'T': case 't':
            if (flag_len == 1
                || ngx_strncasecmp(flag_buf, (u_char *)"type",
                                   flag_len) == 0)
            {
                if (val_len > 0) {
                    rule->forced_mimetype.data =
                        ngx_pnalloc(pool, val_len);
                    if (rule->forced_mimetype.data) {
                        ngx_memcpy(rule->forced_mimetype.data,
                                   val_buf, val_len);
                        rule->forced_mimetype.len = val_len;
                    }
                }
            }
            break;
        }
    }

    return NGX_OK;
}


/*
 * Parse RewriteCond flags: "[NC,OR]"
 */
static ngx_int_t
ngx_http_rewrite_parse_cond_flags(ngx_conf_t *cf,
    ngx_str_t *flags_str, ngx_rewrite_cond_t *cond)
{
    u_char *p, *start, *end;
    u_char  flag_buf[64];
    size_t  flag_len;

    if (flags_str->len == 0) {
        return NGX_OK;
    }

    p = flags_str->data;
    end = p + flags_str->len;

    while (p < end) {
        while (p < end && (*p == ',' || *p == ' ' || *p == '\t')) {
            p++;
        }
        if (p >= end) {
            break;
        }

        start = p;
        while (p < end && *p != ',') {
            p++;
        }

        flag_len = p - start;
        if (flag_len >= sizeof(flag_buf)) flag_len = sizeof(flag_buf) - 1;
        ngx_memcpy(flag_buf, start, flag_len);
        flag_buf[flag_len] = '\0';

        if (ngx_strncasecmp(flag_buf, (u_char *)"NC", 2) == 0
            || ngx_strncasecmp(flag_buf, (u_char *)"nocase",
                               flag_len) == 0)
        {
            cond->flags |= CONDFLAG_NOCASE;
        } else if (ngx_strncasecmp(flag_buf, (u_char *)"OR", 2) == 0
                   || ngx_strncasecmp(flag_buf, (u_char *)"ornext",
                                      flag_len) == 0)
        {
            cond->flags |= CONDFLAG_ORNEXT;
        }
    }

    return NGX_OK;
}


/* ============================================================
 *   Directive handlers
 * ============================================================ */

/*
 * RewriteEngine on|off
 */
static char *
ngx_http_rewrite_engine(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                          *value;
    ngx_http_apache_rewrite_srv_conf_t *sconf = NULL;
    ngx_http_apache_rewrite_loc_conf_t *lcf = NULL;

    value = cf->args->elts;

    /* Определяем контекст: проверяем cmd_type */
    if (cf->cmd_type & NGX_HTTP_LOC_CONF) {
        /* Location контекст */
        lcf = ngx_http_conf_get_module_loc_conf(cf,
                    ngx_http_apache_rewrite_module);
        if (lcf == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
            lcf->state = ENGINE_ENABLED;
            lcf->state_set = 1;
        } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
            lcf->state = ENGINE_DISABLED;
            lcf->state_set = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "RewriteEngine: invalid value \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }

    } else if (cf->cmd_type & NGX_HTTP_SRV_CONF) {
        /* Server контекст */
        sconf = ngx_http_conf_get_module_srv_conf(cf,
                    ngx_http_apache_rewrite_module);
        if (sconf == NULL) {
            return NGX_CONF_OK;
        }

        if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
            sconf->state = ENGINE_ENABLED;
            sconf->state_set = 1;
        } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
            sconf->state = ENGINE_DISABLED;
            sconf->state_set = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "RewriteEngine: invalid value \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }

    } else if (cf->cmd_type & NGX_HTTP_MAIN_CONF) {
        /* Main контекст */
        if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
            sconf = ngx_http_conf_get_module_srv_conf(cf,
                        ngx_http_apache_rewrite_module);
            lcf = ngx_http_conf_get_module_loc_conf(cf,
                        ngx_http_apache_rewrite_module);
            if (sconf && lcf) {
                sconf->state = ENGINE_ENABLED;
                sconf->state_set = 1;
                lcf->state = ENGINE_ENABLED;
                lcf->state_set = 1;
            }
        } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
            sconf = ngx_http_conf_get_module_srv_conf(cf,
                        ngx_http_apache_rewrite_module);
            lcf = ngx_http_conf_get_module_loc_conf(cf,
                        ngx_http_apache_rewrite_module);
            if (sconf && lcf) {
                sconf->state = ENGINE_DISABLED;
                sconf->state_set = 1;
                lcf->state = ENGINE_DISABLED;
                lcf->state_set = 1;
            }
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "RewriteEngine: invalid value \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }

    } else {
        /* Непредвиденный контекст */
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "RewriteEngine: unexpected context");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*
 * Helper: strip surrounding brackets [...]
 */
static void
ngx_rewrite_strip_brackets(ngx_str_t *s)
{
    if (s->len >= 2 && s->data[0] == '['
        && s->data[s->len - 1] == ']')
    {
        s->data++;
        s->len -= 2;
    }
}


/*
 * RewriteRule pattern substitution [flags]
 */
static char *
ngx_http_rewrite_rule(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{

    ngx_http_apache_rewrite_srv_conf_t *sconf;
    ngx_http_apache_rewrite_loc_conf_t *lcf;

    sconf = ngx_http_conf_get_module_srv_conf(cf,
                ngx_http_apache_rewrite_module);
    lcf = ngx_http_conf_get_module_loc_conf(cf,
                ngx_http_apache_rewrite_module);

    void *result = ngx_http_rewrite_rule_helper(0, sconf,
        lcf, cf, NULL, NULL);

    if (result != NULL) {
        return result;
    }

    return NGX_CONF_OK;
}


/*
 * RewriteCond teststring condpattern [flags]
 */
static char *
ngx_http_rewrite_cond(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{

    ngx_http_apache_rewrite_srv_conf_t *sconf;
    ngx_http_apache_rewrite_loc_conf_t *lcf;

    sconf = ngx_http_conf_get_module_srv_conf(cf,
                ngx_http_apache_rewrite_module);
    lcf = ngx_http_conf_get_module_loc_conf(cf,
                ngx_http_apache_rewrite_module);

    void * result = ngx_http_rewrite_cond_helper(0, sconf,
        lcf, cf, NULL, NULL);

    if (result != NULL) {
        return result;
    }

    return NGX_CONF_OK;
}


/*
 * RewriteBase /path
 */
static char *
ngx_http_rewrite_base(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_apache_rewrite_loc_conf_t *lcf = conf;
    ngx_str_t                          *value;

    value = cf->args->elts;

    if (value[1].len == 0 || value[1].data[0] != '/') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "RewriteBase: argument must start with '/'");
        return NGX_CONF_ERROR;
    }

    lcf->baseurl = value[1];
    lcf->baseurl_set = 1;

    return NGX_CONF_OK;
}


/*
 * RewriteOptions option ...
 */
static char *
ngx_http_rewrite_options(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                          *value;
    ngx_uint_t                          i;
    ngx_http_apache_rewrite_srv_conf_t *sconf = NULL;
    ngx_http_apache_rewrite_loc_conf_t *lcf = NULL;

    value = cf->args->elts;

    /* Определяем контекст: проверяем cmd_type */
    if (cf->cmd_type & NGX_HTTP_LOC_CONF) {
        /* Location контекст */
        lcf = ngx_http_conf_get_module_loc_conf(cf,
                    ngx_http_apache_rewrite_module);
        if (lcf == NULL) {
            return NGX_CONF_OK;
        }

        for (i = 1; i < cf->args->nelts; i++) {
            if (ngx_strcasecmp(value[i].data, (u_char *) "Inherit") == 0) {
                lcf->options |= OPTION_INHERIT;
                lcf->options_set = 1;
            } else if (ngx_strcasecmp(value[i].data,
                       (u_char *) "InheritBefore") == 0)
            {
                lcf->options |= OPTION_INHERIT | OPTION_INHERIT_BEFORE;
                lcf->options_set = 1;
            } else {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                    "RewriteOptions: unknown option \"%V\"", &value[i]);
            }
        }

    } else if (cf->cmd_type & NGX_HTTP_SRV_CONF) {
        /* Server контекст */
        sconf = ngx_http_conf_get_module_srv_conf(cf,
                    ngx_http_apache_rewrite_module);
        if (sconf == NULL) {
            return NGX_CONF_OK;
        }

        for (i = 1; i < cf->args->nelts; i++) {
            if (ngx_strcasecmp(value[i].data, (u_char *) "Inherit") == 0) {
                sconf->options |= OPTION_INHERIT;
                sconf->options_set = 1;
            } else if (ngx_strcasecmp(value[i].data,
                       (u_char *) "InheritBefore") == 0)
            {
                sconf->options |= OPTION_INHERIT | OPTION_INHERIT_BEFORE;
                sconf->options_set = 1;
            } else {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                    "RewriteOptions: unknown option \"%V\"", &value[i]);
            }
        }

    } else if (cf->cmd_type & NGX_HTTP_MAIN_CONF) {
        /* Main контекст */
        sconf = ngx_http_conf_get_module_srv_conf(cf,
                    ngx_http_apache_rewrite_module);
        lcf = ngx_http_conf_get_module_loc_conf(cf,
                    ngx_http_apache_rewrite_module);

        for (i = 1; i < cf->args->nelts; i++) {
            if (ngx_strcasecmp(value[i].data, (u_char *) "Inherit") == 0) {
                if (sconf) {
                    sconf->options |= OPTION_INHERIT;
                    sconf->options_set = 1;
                }
                if (lcf) {
                    lcf->options |= OPTION_INHERIT;
                    lcf->options_set = 1;
                }
            } else if (ngx_strcasecmp(value[i].data,
                       (u_char *) "InheritBefore") == 0)
            {
                if (sconf) {
                    sconf->options |= OPTION_INHERIT | OPTION_INHERIT_BEFORE;
                    sconf->options_set = 1;
                }
                if (lcf) {
                    lcf->options |= OPTION_INHERIT | OPTION_INHERIT_BEFORE;
                    lcf->options_set = 1;
                }
            } else {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                    "RewriteOptions: unknown option \"%V\"", &value[i]);
            }
        }

    } else {
        /* Непредвиденный контекст */
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
            "RewriteOptions: unexpected context");
    }

    return NGX_CONF_OK;
}


/*
 * RewriteMap name type:source
 * Example: RewriteMap lc int:tolower
 */
static char *
ngx_http_rewrite_map(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_apache_rewrite_srv_conf_t *sconf = conf;
    ngx_str_t                          *value;
    ngx_rewrite_map_entry_t            *map;
    u_char                             *colon;

    value = cf->args->elts;

    map = ngx_array_push(sconf->maps);
    if (map == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(map, sizeof(ngx_rewrite_map_entry_t));

    map->name = value[1];

    /* Parse type:source */
    colon = ngx_strlchr(value[2].data,
                        value[2].data + value[2].len, ':');
    if (colon == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "RewriteMap: type:source format expected, got \"%V\"",
            &value[2]);
        return NGX_CONF_ERROR;
    }

    {
        ngx_str_t  type_str;
        type_str.data = value[2].data;
        type_str.len = colon - value[2].data;

        map->source.data = colon + 1;
        map->source.len = value[2].len - type_str.len - 1;

        if (ngx_strncasecmp(type_str.data, (u_char *) "int", 3) == 0) {
            map->type = MAPTYPE_INT;

            /* Resolve internal function */
            if (ngx_strncasecmp(map->source.data,
                (u_char *) "tolower", 7) == 0)
            {
                map->func = ngx_rewrite_map_tolower;
            } else if (ngx_strncasecmp(map->source.data,
                       (u_char *) "toupper", 7) == 0)
            {
                map->func = ngx_rewrite_map_toupper;
            } else if (ngx_strncasecmp(map->source.data,
                       (u_char *) "escape", 6) == 0)
            {
                map->func = ngx_rewrite_map_escape;
            } else if (ngx_strncasecmp(map->source.data,
                       (u_char *) "unescape", 8) == 0)
            {
                map->func = ngx_rewrite_map_unescape;
            } else {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                    "RewriteMap: unknown int function \"%V\"",
                    &map->source);
                return NGX_CONF_ERROR;
            }
        } else if (ngx_strncasecmp(type_str.data,
                   (u_char *) "txt", 3) == 0)
        {
            map->type = MAPTYPE_TXT;
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                "RewriteMap: txt maps not yet supported");
        } else if (ngx_strncasecmp(type_str.data,
                   (u_char *) "rnd", 3) == 0)
        {
            map->type = MAPTYPE_RND;
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                "RewriteMap: rnd maps not yet supported");
        } else if (ngx_strncasecmp(type_str.data,
                   (u_char *) "prg", 3) == 0)
        {
            map->type = MAPTYPE_PRG;
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                "RewriteMap: prg maps not yet supported");
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                "RewriteMap: unknown map type \"%V\"", &type_str);
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


/*
 * HtaccessEnable on|off — enable/disable .htaccess parsing
 */
static char *
ngx_http_htaccess_enable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                          *value;
    ngx_http_apache_rewrite_srv_conf_t *sconf;

    value = cf->args->elts;


    sconf = ngx_http_conf_get_module_srv_conf(cf,
                ngx_http_apache_rewrite_module);
    if (sconf == NULL) {
        return NGX_CONF_OK; /* skip action when no server config available */
    }

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        sconf->htaccess_enable = 1;
        sconf->htaccess_enable_set = 1;
    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        sconf->htaccess_enable = 0;
        sconf->htaccess_enable_set = 1;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "HtaccessEnable: invalid value \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*
 * HtaccessName <filename> — set alternative filename for htaccess file
 */
static char *
ngx_http_htaccess_name(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                          *value;
    ngx_http_apache_rewrite_srv_conf_t *sconf;

    sconf = ngx_http_conf_get_module_srv_conf(cf,
                ngx_http_apache_rewrite_module);

    value = cf->args->elts;

    if (sconf == NULL) {
        return NGX_CONF_OK; /* skip action when no server config available */
    }

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "HtaccessName: argument must not be empty");
        return NGX_CONF_ERROR;
    }

    sconf->htaccess_name = value[1];
    sconf->htaccess_name_set = 1;

    return NGX_CONF_OK;
}


/*
 * FallbackToIndex on|off — enable automatic fallback to index.php with query string
 * when try_files fails (transformation result file not found)
 */
static char *
ngx_http_fallback_to_index(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                          *value;
    ngx_http_apache_rewrite_srv_conf_t *sconf;

    value = cf->args->elts;

    sconf = ngx_http_conf_get_module_srv_conf(cf,
                ngx_http_apache_rewrite_module);

    if (sconf == NULL) {
        return NGX_CONF_OK; /* skip action when no server config available */
    }

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        sconf->fallback_to_index = 1;
        sconf->fallback_to_index_set = 1;
    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        sconf->fallback_to_index = 0;
        sconf->fallback_to_index_set = 1;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "FallbackToIndex: invalid value \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*
 * Apply FallbackToIndex logic in URL register hook.
 * If file exists after transformation → proceed normally.
 * If file missing → redirect to fallback_path (from RewriteFallBack in .htaccess or /index.php).
 */
static ngx_int_t
ngx_http_apache_rewrite_url_register_hook_with_fallback(ngx_http_request_t *r)
{
    ngx_str_t          path, args, fallback_buf;
    ngx_http_core_loc_conf_t  *clcf;
    struct stat        st;
    ngx_rewrite_ctx_t  *ctx;

    /* Fallback enabled — check file existence first */

    size_t root;
    ngx_str_t r_path;
    u_char *last;

    ngx_int_t fallback_needed = 0;

    last = ngx_http_map_uri_to_path(r, &r_path, &root, 0);
    if (last == NULL) {
        fallback_needed = 1;
    } else {
        r_path.len = last - r_path.data;
        r_path.data = r_path.data;

        u_char *r_path_null = ngx_pcalloc(r->pool, r_path.len + 1);
        if (!r_path_null) {
            return NGX_DECLINED;
        }

        ngx_memcpy(r_path_null, r_path.data, r_path.len);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite: fallback check for file %s",
                       r_path_null);

        if (stat((const char *)r_path_null, &st) != 0) {
            fallback_needed = 1;
        }
    }
    if (fallback_needed) {
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        if (clcf == NULL) {
            return NGX_DECLINED;
        }

        /* Initialize path and args from current URI */
        path.data = r->uri.data;
        path.len = r->uri.len;
        args.data = r->args.data;
        args.len = r->args.len;

        /* Get fallback path from context (from .htaccess RewriteFallBack or default /index.php) */
        ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
        if (!ctx || !ctx->fallback_path.data || ctx->fallback_path.len == 0) {
            /* No context fallback — use default /index.php */
            ngx_str_set(&path, "/index.php");
            path.len = 10;
        } else {
            /* Use fallback path from .htaccess RewriteFallBack directive */
            path.data = ctx->fallback_path.data;
            path.len = ctx->fallback_path.len;
        }

        if (!args.data) {
            ngx_http_split_args(r, &path, &args);
        }

        /* Build fallback path with query string */
        fallback_buf.data = ngx_pnalloc(r->pool, path.len + 3 + (args.data ? args.len : 0));
        if (fallback_buf.data == NULL) {
            return NGX_DECLINED;
        }

        fallback_buf.len = 0;

        /* Add fallback path */
        ngx_memcpy(fallback_buf.data , path.data, path.len);
        fallback_buf.len = path.len;

        r->uri = fallback_buf;

        path = r->uri;

        /* Reset args for internal redirect */
        args = r->args;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "mod_rewrite: fallback to \"%V?%V\" after try_files miss",
                       &r->uri, &r->args);


        (void)ngx_http_internal_redirect(r, &path, &args);
        ngx_http_finalize_request(r, NGX_DONE);
        return NGX_DONE;

    } else {
        //that means the path correct and exists
        return NGX_DECLINED;
    }

}
