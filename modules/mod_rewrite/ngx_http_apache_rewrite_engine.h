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
 * ngx_http_apache_rewrite_engine.h
 *
 * Shared header for the nginx Apache mod_rewrite compatibility module.
 * Contains all data structures, constants, and function prototypes.
 */

#ifndef _NGX_HTTP_APACHE_REWRITE_ENGINE_H_
#define _NGX_HTTP_APACHE_REWRITE_ENGINE_H_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/*
 * Maximum regex captures (matches AP_MAX_REG_MATCH = 10)
 */
#define NGX_REWRITE_MAX_CAPTURES    10

/*
 * Maximum rounds for [N] flag loop prevention
 */
#define NGX_REWRITE_MAX_ROUNDS      10000

/*
 * RewriteEngine state
 */
#define ENGINE_DISABLED             (1 << 0)
#define ENGINE_ENABLED              (1 << 1)

/*
 * RewriteOptions flags
 */
#define OPTION_NONE                 (1 << 0)
#define OPTION_INHERIT              (1 << 1)
#define OPTION_INHERIT_BEFORE       (1 << 2)

/*
 * RewriteRule flags
 */
#define RULEFLAG_NONE               (1 << 0)
#define RULEFLAG_FORCEREDIRECT      (1 << 1)
#define RULEFLAG_LASTRULE           (1 << 2)
#define RULEFLAG_NEWROUND           (1 << 3)
#define RULEFLAG_CHAIN              (1 << 4)
#define RULEFLAG_IGNOREONSUBREQ     (1 << 5)
#define RULEFLAG_NOTMATCH           (1 << 6)
#define RULEFLAG_PROXY              (1 << 7)
#define RULEFLAG_PASSTHROUGH        (1 << 8)
#define RULEFLAG_QSAPPEND           (1 << 9)
#define RULEFLAG_NOCASE             (1 << 10)
#define RULEFLAG_NOESCAPE           (1 << 11)
#define RULEFLAG_NOSUB              (1 << 12)
#define RULEFLAG_STATUS             (1 << 13)
#define RULEFLAG_ESCAPEBACKREF      (1 << 14)
#define RULEFLAG_DISCARDPATHINFO    (1 << 15)
#define RULEFLAG_QSDISCARD          (1 << 16)
#define RULEFLAG_END                (1 << 17)
#define RULEFLAG_QSLAST             (1 << 19)

/*
 * Return codes for apply_rewrite_list
 */
#define ACTION_NORMAL               (1 << 0)
#define ACTION_NOESCAPE             (1 << 1)
#define ACTION_STATUS               (1 << 2)
#define ACTION_STATUS_SET           (1 << 3)

/*
 * RewriteCond flags
 */
#define CONDFLAG_NONE               (1 << 0)
#define CONDFLAG_NOCASE             (1 << 1)
#define CONDFLAG_NOTMATCH           (1 << 2)
#define CONDFLAG_ORNEXT             (1 << 3)

/*
 * Condition pattern types
 */
typedef enum {
    CONDPAT_REGEX = 0,
    CONDPAT_FILE_EXISTS,
    CONDPAT_FILE_SIZE,
    CONDPAT_FILE_LINK,
    CONDPAT_FILE_DIR,
    CONDPAT_FILE_XBIT,
    CONDPAT_STR_LT,
    CONDPAT_STR_LE,
    CONDPAT_STR_EQ,
    CONDPAT_STR_GT,
    CONDPAT_STR_GE,
    CONDPAT_INT_LT,
    CONDPAT_INT_LE,
    CONDPAT_INT_EQ,
    CONDPAT_INT_GT,
    CONDPAT_INT_GE
} ngx_rewrite_condpat_type_e;

/*
 * Rule return types
 */
typedef enum {
    RULE_RC_NOMATCH = 0,
    RULE_RC_MATCH = 1,
    RULE_RC_NOSUB = 2,
    RULE_RC_STATUS_SET = 3
} ngx_rewrite_rule_rc_e;

/*
 * Condition return types
 */
typedef enum {
    COND_RC_NOMATCH = 0,
    COND_RC_MATCH = 1
} ngx_rewrite_cond_rc_e;

/*
 * Map types
 */
#define MAPTYPE_TXT                 (1 << 0)
#define MAPTYPE_INT                 (1 << 3)
#define MAPTYPE_RND                 (1 << 4)
#define MAPTYPE_PRG                 (1 << 2)

/*
 * Backreference info — stores match source + capture offsets
 */
typedef struct {
    ngx_str_t    source;
    int          ovector[NGX_REWRITE_MAX_CAPTURES * 2];
    ngx_int_t    ncaptures;
} ngx_rewrite_backref_t;

/*
 * Single linked list for env vars (E=var:val)
 */
typedef struct ngx_rewrite_data_item_s ngx_rewrite_data_item_t;
struct ngx_rewrite_data_item_s {
    ngx_rewrite_data_item_t  *next;
    ngx_str_t                 data;
};

/*
 * RewriteCond entry
 */
typedef struct {
    ngx_str_t                     input;       /* input string */
    ngx_str_t                     pattern;     /* pattern string */
    ngx_regex_t                  *regex;       /* compiled regex (if regex type) */
    ngx_int_t                     flags;       /* CONDFLAG_* */
    ngx_rewrite_condpat_type_e    ptype;       /* pattern type */
} ngx_rewrite_cond_t;

/*
 * RewriteRule entry
 */
typedef struct {
    ngx_str_t                     pattern;      /* regex pattern string */
    ngx_regex_t                  *regex;        /* compiled regex */
    ngx_str_t                     output;       /* substitution string */
    ngx_int_t                     flags;        /* RULEFLAG_* */
    ngx_array_t                  *conditions;   /* array of ngx_rewrite_cond_t */
    ngx_int_t                     skip;         /* S=N skip count */
    ngx_int_t                     maxrounds;    /* N=limit */
    ngx_int_t                     forced_responsecode; /* R=NNN */
    ngx_rewrite_data_item_t      *env;          /* E=var:val list */
    ngx_str_t                     forced_mimetype; /* T=type */
} ngx_rewrite_rule_t;

/*
 * htaccess cache linked list entry
 */
typedef struct htaccess_entry_s {
    u_char        *file_path;           /* full path to .htaccess file (string) */
    time_t        mtime;                /* last modification time */
    ngx_array_t  *rules;                /* array of ngx_rewrite_rule_t */
    ngx_str_t       baseurl;
    ngx_int_t       options;
    ngx_int_t       state;
    unsigned        state_set:1;
    unsigned        options_set:1;
    unsigned        baseurl_set:1;
    ngx_str_t       fallback_path;      /* RewriteFallBack path from .htaccess */
    struct htaccess_entry_s *next;      /* linked list next pointer */
} htaccess_entry_t;

/*
 * Map entry
 */
typedef struct {
    ngx_str_t   name;
    ngx_int_t   type;             /* MAPTYPE_* */
    ngx_str_t   source;           /* filename or function name for int */
    ngx_str_t (*func)(ngx_pool_t *pool, ngx_str_t key);
} ngx_rewrite_map_entry_t;

/*
 * Per-server configuration
 */
typedef struct {
    ngx_int_t       state;        /* ENGINE_DISABLED | ENGINE_ENABLED */
    ngx_int_t       options;
    ngx_array_t    *rules;        /* array of ngx_rewrite_rule_t */
    ngx_array_t    *pending_conds;/* temp: conditions before next rule */
    ngx_array_t    *maps;         /* array of ngx_rewrite_map_entry_t */
    unsigned        state_set:1;
    unsigned        options_set:1;
    ngx_int_t       htaccess_enable;      // 0|1: enable htaccess parsing
    ngx_str_t       htaccess_name;        // file name (default ".htaccess")
    unsigned        htaccess_enable_set:1;
    unsigned        htaccess_name_set:1;

    /* Fallback to index.php with query string after try_files miss */
    ngx_int_t       fallback_to_index;    // 0|1: enable fallback mechanism
    unsigned        fallback_to_index_set:1;
} ngx_http_apache_rewrite_srv_conf_t;

/*
 * Per-location configuration
 */
typedef struct {
    ngx_int_t       state;        /* ENGINE_DISABLED | ENGINE_ENABLED */
    ngx_int_t       options;
    ngx_array_t    *rules;        /* array of ngx_rewrite_rule_t */
    ngx_array_t    *pending_conds;/* temp: conditions before next rule */
    ngx_str_t       baseurl;      /* RewriteBase */
    unsigned        state_set:1;
    unsigned        options_set:1;
    unsigned        baseurl_set:1;
    ngx_str_t       fallback_path;  /* RewriteFallBack path from .htaccess */
} ngx_http_apache_rewrite_loc_conf_t;

/*
 * Per-request context (stored via ngx_http_set_ctx / ngx_http_get_module_ctx)
 */
typedef struct {
    ngx_http_request_t   *r;
    ngx_str_t             uri;      /* current URI being matched */
    ngx_str_t             match_uri;
    ngx_str_t             htaccess;
    ngx_rewrite_backref_t briRR;    /* rule backrefs ($N) */
    ngx_rewrite_backref_t briRC;    /* condition backrefs (%N) */
    unsigned              end:1;    /* END flag set — stop all rewriting */
    ngx_str_t             redirect_url; /* for external redirects */
    ngx_int_t             redirect_code;
    ngx_int_t             status_code;  /* forced status from [F], [G], etc. */
    htaccess_entry_t     *htaccess_cache_head;  /* head of linked list of cached entries */

    /* Fallback path - from RewriteFallBack in .htaccess or default /index.php */
    ngx_str_t             fallback_path;

    /* Environment variables set by [E=VAR:VAL] flags */
    /* These persist across request phases for FastCGI integration */
    ngx_rewrite_data_item_t   *env_vars;
    ngx_int_t             uri_changed;
} ngx_rewrite_ctx_t;

/* modules/mod_rewrite/ngx_http_apache_rewrite_pool_ctx.h */
typedef struct {
    ngx_flag_t             skip_rewrite_after_redirect; /* флаг пропуска rewrite */
} ngx_rewrite_pool_ctx_t;

/*
 * htaccess pool cleanup context - stores cached entries when request ctx is cleared
 */
typedef struct {
    htaccess_entry_t     *htaccess_cache_entries_head;  /* head of linked list of cached entries (saved in pool) */
} ngx_htaccess_pool_cleanup_ctx_t;

/*
 * Module extern
 */
extern ngx_module_t ngx_http_apache_rewrite_module;


/* --- Engine functions (ngx_http_apache_rewrite_engine.c) --- */

ngx_int_t ngx_rewrite_apply_list(ngx_http_request_t *r,
    ngx_array_t *rules, ngx_str_t *htaccess, ngx_str_t baseurl, ngx_int_t baseurl_set);

ngx_rewrite_rule_rc_e ngx_rewrite_apply_rule(ngx_rewrite_rule_t *rule,
    ngx_rewrite_ctx_t *ctx, ngx_str_t baseurl, ngx_int_t baseurl_set);

ngx_rewrite_cond_rc_e ngx_rewrite_apply_cond(ngx_rewrite_cond_t *cond,
    ngx_rewrite_ctx_t *ctx);

void ngx_rewrite_splitout_queryargs(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_int_t flags, ngx_str_t *args);

ngx_int_t ngx_rewrite_is_absolute_uri(ngx_str_t *uri);


/* --- Expand functions (ngx_http_apache_rewrite_expand.c) --- */

ngx_str_t ngx_rewrite_expand(ngx_str_t *input, ngx_rewrite_ctx_t *ctx,
    ngx_rewrite_rule_t *rule, ngx_pool_t *pool);


/* --- Variable functions (ngx_http_apache_rewrite_variable.c) --- */

ngx_str_t ngx_rewrite_lookup_variable(ngx_str_t *var,
    ngx_rewrite_ctx_t *ctx);


/* --- Map functions (ngx_http_apache_rewrite_map.c) --- */

ngx_str_t ngx_rewrite_lookup_map(ngx_http_request_t *r,
    ngx_http_apache_rewrite_srv_conf_t *sconf,
    ngx_str_t *mapname, ngx_str_t *key);

ngx_str_t ngx_rewrite_map_tolower(ngx_pool_t *pool, ngx_str_t key);
ngx_str_t ngx_rewrite_map_toupper(ngx_pool_t *pool, ngx_str_t key);
ngx_str_t ngx_rewrite_map_escape(ngx_pool_t *pool, ngx_str_t key);
ngx_str_t ngx_rewrite_map_unescape(ngx_pool_t *pool, ngx_str_t key);

/* reuest cache functions */

ngx_int_t
ngx_rewrite_set_skip_after_redirect(ngx_http_request_t *r);


#endif /* _NGX_HTTP_APACHE_REWRITE_ENGINE_H_ */
