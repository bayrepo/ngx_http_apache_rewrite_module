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
 * ngx_http_apache_rewrite_engine.c
 *
 * Core rewrite engine for nginx Apache mod_rewrite module.
 * Ports apply_rewrite_list, apply_rewrite_rule, apply_rewrite_cond
 * from Apache mod_rewrite.c.
 */

#include "ngx_http_apache_rewrite_engine.h"


/*
 * Check if a URI is absolute (has scheme://)
 */
ngx_int_t
ngx_rewrite_is_absolute_uri(ngx_str_t *uri)
{
    u_char  *p, *end;

    if (uri->len < 4) {
        return 0;
    }

    p = uri->data;
    end = p + uri->len;

    /* Must start with alpha */
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
        return 0;
    }

    for (p++; p < end; p++) {
        if (*p == ':') {
            if (p + 2 < end && p[1] == '/' && p[2] == '/') {
                return 1;
            }
            return 0;
        }
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
              || (*p >= '0' && *p <= '9') || *p == '+' || *p == '-'
              || *p == '.'))
        {
            return 0;
        }
    }

    return 0;
}


/*
 * Split query string out of a rewritten URI.
 * Handles QSA (append), QSD (discard) flags.
 */
void
ngx_rewrite_splitout_queryargs(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_int_t flags, ngx_str_t *args)
{
    u_char  *q;
    u_char  *search_start;
    u_char  *search_end;

    if (flags & RULEFLAG_QSDISCARD) {
        ngx_str_null(args);
    }

    search_start = uri->data;
    search_end = uri->data + uri->len;

    /* Find the question mark */
    if (flags & RULEFLAG_QSLAST) {
        /* Search from end */
        q = NULL;
        for (u_char *s = search_end - 1; s >= search_start; s--) {
            if (*s == '?') {
                q = s;
                break;
            }
        }
    } else {
        /* Search from start */
        q = ngx_strlchr(search_start, search_end, '?');
    }

    if (q == NULL) {
        return;
    }

    /* Split: uri becomes everything before '?', args is after */
    if (flags & RULEFLAG_QSAPPEND) {
        /* Append new query string to existing */
        if ((size_t)(search_end - q - 1) > 0) {
            if (args->len > 0) {
                u_char *combined;
                size_t  new_len = (search_end - q - 1) + 1 + args->len;

                combined = ngx_pnalloc(r->pool, new_len);
                if (combined) {
                    u_char *cp = combined;
                    cp = ngx_cpymem(cp, q + 1, search_end - q - 1);
                    *cp++ = '&';
                    cp = ngx_cpymem(cp, args->data, args->len);
                    args->data = combined;
                    args->len = new_len;
                }
            } else {
                args->data = q + 1;
                args->len = search_end - q - 1;
            }
        }
    } else if (!(flags & RULEFLAG_QSDISCARD)) {
        args->data = q + 1;
        args->len = search_end - q - 1;
    }

    uri->len = q - uri->data;
}




/*
 * Apply a single RewriteCond.
 * Port of Apache apply_rewrite_cond() (mod_rewrite.c:4120-4319).
 */
ngx_rewrite_cond_rc_e
ngx_rewrite_apply_cond(ngx_rewrite_cond_t *cond, ngx_rewrite_ctx_t *ctx)
{
    ngx_str_t    input;
    ngx_int_t    rc = COND_RC_NOMATCH;
    ngx_int_t    n;
    int          ovector[NGX_REWRITE_MAX_CAPTURES * 2];
    ngx_file_info_t  fi;

    /* Expand the condition input string */
    input = ngx_rewrite_expand(&cond->input, ctx, NULL, ctx->r->pool);

    switch (cond->ptype) {

    case CONDPAT_FILE_EXISTS:
        if (input.len > 0) {
            u_char buf[NGX_MAX_PATH];
            ngx_cpystrn(buf, input.data, ngx_min(input.len + 1, NGX_MAX_PATH));
            if (ngx_file_info(buf, &fi) == NGX_FILE_ERROR) {
                rc = COND_RC_NOMATCH;
            } else if (ngx_is_file(&fi)) {
                rc = COND_RC_MATCH;
            }
        }
        break;

    case CONDPAT_FILE_SIZE:
        if (input.len > 0) {
            u_char buf[NGX_MAX_PATH];
            ngx_cpystrn(buf, input.data, ngx_min(input.len + 1, NGX_MAX_PATH));
            if (ngx_file_info(buf, &fi) != NGX_FILE_ERROR
                && ngx_is_file(&fi) && ngx_file_size(&fi) > 0)
            {
                rc = COND_RC_MATCH;
            }
        }
        break;

    case CONDPAT_FILE_DIR:
        if (input.len > 0) {
            u_char buf[NGX_MAX_PATH];
            ngx_cpystrn(buf, input.data, ngx_min(input.len + 1, NGX_MAX_PATH));
            if (ngx_file_info(buf, &fi) != NGX_FILE_ERROR
                && ngx_is_dir(&fi))
            {
                rc = COND_RC_MATCH;
            }
        }
        break;

    case CONDPAT_FILE_LINK:
        if (input.len > 0) {
            u_char buf[NGX_MAX_PATH];
            ngx_cpystrn(buf, input.data, ngx_min(input.len + 1, NGX_MAX_PATH));
            if (ngx_file_info(buf, &fi) != NGX_FILE_ERROR
                && ngx_is_link(&fi))
            {
                rc = COND_RC_MATCH;
            }
        }
        break;

    case CONDPAT_FILE_XBIT:
        if (input.len > 0) {
            u_char buf[NGX_MAX_PATH];
            ngx_cpystrn(buf, input.data, ngx_min(input.len + 1, NGX_MAX_PATH));
            if (ngx_file_info(buf, &fi) != NGX_FILE_ERROR
                && (ngx_file_access(&fi) & 0111))
            {
                rc = COND_RC_MATCH;
            }
        }
        break;

    case CONDPAT_STR_LT:
        rc = (ngx_memn2cmp(input.data, cond->pattern.data,
                           input.len, cond->pattern.len) < 0)
             ? COND_RC_MATCH : COND_RC_NOMATCH;
        break;

    case CONDPAT_STR_LE:
        rc = (ngx_memn2cmp(input.data, cond->pattern.data,
                           input.len, cond->pattern.len) <= 0)
             ? COND_RC_MATCH : COND_RC_NOMATCH;
        break;

    case CONDPAT_STR_EQ:
        if (cond->flags & CONDFLAG_NOCASE) {
            rc = (input.len == cond->pattern.len
                  && ngx_strncasecmp(input.data, cond->pattern.data,
                                     input.len) == 0)
                 ? COND_RC_MATCH : COND_RC_NOMATCH;
        } else {
            rc = (input.len == cond->pattern.len
                  && ngx_memcmp(input.data, cond->pattern.data,
                                input.len) == 0)
                 ? COND_RC_MATCH : COND_RC_NOMATCH;
        }
        break;

    case CONDPAT_STR_GT:
        rc = (ngx_memn2cmp(input.data, cond->pattern.data,
                           input.len, cond->pattern.len) > 0)
             ? COND_RC_MATCH : COND_RC_NOMATCH;
        break;

    case CONDPAT_STR_GE:
        rc = (ngx_memn2cmp(input.data, cond->pattern.data,
                           input.len, cond->pattern.len) >= 0)
             ? COND_RC_MATCH : COND_RC_NOMATCH;
        break;

    case CONDPAT_INT_LT:
    case CONDPAT_INT_LE:
    case CONDPAT_INT_EQ:
    case CONDPAT_INT_GT:
    case CONDPAT_INT_GE:
        {
            ngx_int_t  a, b;
            u_char    *tmp;

            /* null-terminate for ngx_atoi */
            tmp = ngx_pnalloc(ctx->r->pool, input.len + 1);
            if (tmp) {
                ngx_memcpy(tmp, input.data, input.len);
                tmp[input.len] = '\0';
                a = ngx_atoi(input.data, input.len);
            } else {
                a = 0;
            }
            b = ngx_atoi(cond->pattern.data, cond->pattern.len);

            if (a == NGX_ERROR) a = 0;
            if (b == NGX_ERROR) b = 0;

            switch (cond->ptype) {
            case CONDPAT_INT_LT: rc = (a < b);  break;
            case CONDPAT_INT_LE: rc = (a <= b); break;
            case CONDPAT_INT_EQ: rc = (a == b); break;
            case CONDPAT_INT_GT: rc = (a > b);  break;
            case CONDPAT_INT_GE: rc = (a >= b); break;
            default: break;
            }
        }
        break;

    default:
        /* CONDPAT_REGEX — regular expression match */
        if (cond->regex == NULL) {
            break;
        }

        n = ngx_regex_exec(cond->regex, &input,
                           ovector, NGX_REWRITE_MAX_CAPTURES * 2);

        if (n >= 0) {
            rc = COND_RC_MATCH;

            /* Store condition backrefs (%N) */
            if (!(cond->flags & CONDFLAG_NOTMATCH)) {
                ctx->briRC.source = input;
                ngx_memcpy(ctx->briRC.ovector, ovector, sizeof(ovector));
                ctx->briRC.ncaptures = (n > 0) ? n : NGX_REWRITE_MAX_CAPTURES;
            }
        }
        break;
    }

    /* Handle negation */
    if (cond->flags & CONDFLAG_NOTMATCH) {
        rc = !rc;
    }

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, ctx->r->connection->log, 0,
                   "mod_rewrite: RewriteCond input=\"%V\" pattern=\"%V\" "
                   "%s => %s",
                   &input, &cond->pattern,
                   (cond->flags & CONDFLAG_NOCASE) ? "[NC]" : "",
                   rc ? "matched" : "not-matched");

    return rc ? COND_RC_MATCH : COND_RC_NOMATCH;
}


/*
 * Apply a single RewriteRule.
 * Port of Apache apply_rewrite_rule() (mod_rewrite.c:4359-4641).
 */
ngx_rewrite_rule_rc_e
ngx_rewrite_apply_rule(ngx_rewrite_rule_t *rule, ngx_rewrite_ctx_t *ctx, ngx_str_t baseurl, ngx_int_t baseurl_set)
{
    int                     ovector[NGX_REWRITE_MAX_CAPTURES * 2];
    ngx_int_t               n, rc;
    ngx_rewrite_cond_t     *conds;
    ngx_uint_t              i;
    ngx_str_t               newuri;
    ngx_http_request_t     *r = ctx->r;

    /* Match the URI against the rule pattern */
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite: applying pattern \"%V\" to uri \"%V\"",
                   &rule->pattern, &ctx->uri);

    if (rule->regex == NULL) {
        return RULE_RC_NOMATCH;
    }

    n = ngx_regex_exec(rule->regex, &ctx->match_uri,
                       ovector, NGX_REWRITE_MAX_CAPTURES * 2);

    rc = (n >= 0) ? 1 : 0;

    /* Check match/notmatch */
    if (!((rc && !(rule->flags & RULEFLAG_NOTMATCH))
          || (!rc && (rule->flags & RULEFLAG_NOTMATCH))))
    {
        return RULE_RC_NOMATCH;
    }

    /* Rule matched — prepare backref context */
    ngx_str_null(&ctx->briRC.source);
    ctx->briRC.ncaptures = 0;

    if (rule->flags & RULEFLAG_NOTMATCH) {
        ngx_str_null(&ctx->briRR.source);
        ctx->briRR.ncaptures = 0;
    } else {
        ctx->briRR.source.data = ngx_pnalloc(r->pool, ctx->uri.len);
        if (ctx->briRR.source.data) {
            ngx_memcpy(ctx->briRR.source.data, ctx->match_uri.data, ctx->match_uri.len);
            ctx->briRR.source.len = ctx->match_uri.len;
        }
        ngx_memcpy(ctx->briRR.ovector, ovector, sizeof(ovector));
        ctx->briRR.ncaptures = (n > 0) ? n : NGX_REWRITE_MAX_CAPTURES;
    }

    /* Evaluate conditions (AND/OR logic) */
    if (rule->conditions && rule->conditions->nelts > 0) {
        conds = rule->conditions->elts;

        for (i = 0; i < rule->conditions->nelts; i++) {
            ngx_rewrite_cond_t *c = &conds[i];

            rc = ngx_rewrite_apply_cond(c, ctx);

            if (c->flags & CONDFLAG_ORNEXT) {
                if (!rc) {
                    /* OR: this one failed, try next */
                    continue;
                } else {
                    /* OR: this one matched, skip remaining OR chain */
                    while (i < rule->conditions->nelts
                           && conds[i].flags & CONDFLAG_ORNEXT)
                    {
                        i++;
                    }
                }
            } else if (!rc) {
                /* AND: condition failed — rule does not match */
                return RULE_RC_NOMATCH;
            }
        }
    }

    /* Handle environment variables (E=var:val) */
    if (rule->env) {
        ngx_rewrite_data_item_t *env = rule->env;
        while (env) {
            ngx_str_t expanded;
            u_char   *colon;

            expanded = ngx_rewrite_expand(&env->data, ctx, rule, r->pool);

            if (expanded.len > 0 && expanded.data[0] == '!') {
                /* Unset: not directly supported in nginx, skip */
            } else {
                colon = ngx_strlchr(expanded.data,
                                    expanded.data + expanded.len, ':');
                if (colon) {
                    ngx_str_t vname, vval;
                    (void)vval;

                    vname.data = expanded.data;
                    vname.len = colon - expanded.data;
                    vval.data = colon + 1;
                    vval.len = expanded.len - vname.len - 1;

                    /* Store in context for FastCGI integration */
                    if (ctx->env_vars == NULL) {
                        ctx->env_vars = ngx_palloc(r->pool,
                            sizeof(ngx_rewrite_data_item_t));
                        ctx->env_vars->next = NULL;
                        ctx->env_vars->data.data = ngx_pnalloc(r->pool, expanded.len);
                        if (ctx->env_vars->data.data) {
                            ngx_memcpy(ctx->env_vars->data.data, expanded.data, expanded.len);
                            ctx->env_vars->data.len = expanded.len;
                        } else {
                            /* Memory allocation failed - continue without tracking */
                        }
                    } else {
                        /* Append to list */
                        ngx_rewrite_data_item_t *new_env = ngx_palloc(r->pool,
                            sizeof(ngx_rewrite_data_item_t));
                        if (new_env) {
                            new_env->data.data = ngx_pnalloc(r->pool, expanded.len);
                            if (new_env->data.data) {
                                ngx_memcpy(new_env->data.data, expanded.data, expanded.len);
                                new_env->data.len = expanded.len;
                            } else {
                                /* Memory allocation failed */
                                new_env = NULL;
                            }
                            new_env->next = ctx->env_vars;
                            if (new_env) {
                                ctx->env_vars = new_env;
                            }
                        }
                    }

                    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                                   "mod_rewrite: [E=...] set \"%*s\" => \"%*s\"",
                                   vname.len, vname.data, vval.len, vval.data);
                }
            }

            env = env->next;
        }
    }

    /* No-substitution rule (pattern '-') */
    if (rule->flags & RULEFLAG_NOSUB) {
        if (rule->flags & RULEFLAG_STATUS) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "mod_rewrite: forcing status %d (nosub)",
                           rule->forced_responsecode);
        }
        return RULE_RC_NOSUB;
    }

    /* Expand the substitution */
    newuri = ngx_rewrite_expand(&rule->output, ctx, rule, r->pool);

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "mod_rewrite: rewrite \"%V\" -> \"%V\" (flags=0x%xd, code=%d)",
                   &ctx->uri, &newuri, rule->flags, rule->forced_responsecode);

    /* Split out query string */
    {
        ngx_str_t  new_args = r->args;
        ngx_rewrite_splitout_queryargs(r, &newuri, rule->flags, &new_args);
        r->args = new_args;
    }

    /* Check for absolute URI → redirect */
    if (rule->flags & RULEFLAG_FORCEREDIRECT) {
        /* Explicit redirect [R] or [R=NNN] */
        ngx_int_t code = rule->forced_responsecode;
        if (code == 0) {
            code = NGX_HTTP_MOVED_TEMPORARILY;
        }

        /* Fully qualify if needed */
        if (!ngx_rewrite_is_absolute_uri(&newuri)) {
            ngx_str_t  scheme, host;
            ngx_uint_t port;
            u_char    *p;
            size_t     len;

#if (NGX_SSL)
            if (r->connection->ssl) {
                ngx_str_set(&scheme, "https");
                port = 443;
            } else
#endif
            {
                ngx_str_set(&scheme, "http");
                port = 80;
            }

            ngx_http_core_loc_conf_t  *clcf;
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            /* Use r->headers_in.server if available (has been sanitized - no port) */
            if (clcf->server_name_in_redirect || r->headers_in.server.len > 0) {
                if (clcf->server_name_in_redirect) {
                    ngx_http_core_srv_conf_t *cscf;
                    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
                    host = cscf->server_name;
                } else {
                    host = r->headers_in.server;
                }
            } else {
                ngx_http_core_srv_conf_t *cscf;
                cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
                host = cscf->server_name;
            }

            {
                ngx_uint_t actual_port;
                actual_port = ngx_inet_get_port(r->connection->local_sockaddr);
                if (actual_port == port) {
                    port = 0;
                } else {
                    port = actual_port;
                }
            }

            len = scheme.len + 3 + host.len + newuri.len + 3;
            if (port) {
                len += 6; /* :NNNNN */
            }
            if (baseurl_set) {
                len += baseurl.len;
            }

            p = ngx_pnalloc(r->pool, len);
            if (p) {
                u_char *start = p;
                p = ngx_cpymem(p, scheme.data, scheme.len);
                *p++ = ':'; *p++ = '/'; *p++ = '/';
                p = ngx_cpymem(p, host.data, host.len);
                if (port) {
                    p = ngx_sprintf(p, ":%ui", port);
                }
                if (baseurl_set && baseurl.len > 0) {
                    if (baseurl.data[0] == '/')
                        p = ngx_cpymem(p, baseurl.data, baseurl.len);
                    else {
                        *p++ = '/';
                        p = ngx_cpymem(p, baseurl.data, baseurl.len);
                    }
                }
                if (*(p-1)!='/' && newuri.data[0]!= '/') {
                    *p++ = '/';
                }
                p = ngx_cpymem(p, newuri.data, newuri.len);
                newuri.data = start;
                newuri.len = p - start;
            }
        }

        ctx->redirect_url = newuri;
        ctx->redirect_code = code;
        ctx->uri = newuri;
        return RULE_RC_MATCH;
    }

    /* Check for implicit redirect (absolute URI in substitution) */
    if (ngx_rewrite_is_absolute_uri(&newuri)) {
        ngx_int_t code = rule->forced_responsecode;
        if (code == 0) {
            code = NGX_HTTP_MOVED_TEMPORARILY;
        }

        ctx->redirect_url = newuri;
        ctx->redirect_code = code;
        ctx->uri = newuri;
        return RULE_RC_MATCH;
    }

    /* Ensure URI starts with '/' */
    if (newuri.len == 0 || newuri.data[0] != '/') {
        if (ctx->htaccess.data){
            u_char *p = ngx_pcalloc(r->pool, ctx->htaccess.len + newuri.len + 4);
            if (p){
                *p = '/';
                u_char *p1 = p + 1;
                if (ctx->htaccess.len > 0){
                    ngx_memcpy(p1, ctx->htaccess.data, ctx->htaccess.len);
                    p1 = p1 + ctx->htaccess.len;
                    if (ctx->htaccess.data[ctx->htaccess.len-1] != '/') {
                        *p1 = '/';
                        p1++;
                    }
                }
                if (newuri.len > 0) {
                    ngx_memcpy(p1, newuri.data, newuri.len);
                    p1 = p1 + newuri.len;
                }
                newuri.data = p;
                newuri.len = p1 - p;
            }
        } else {
            u_char *p = ngx_pcalloc(r->pool, newuri.len + 1);
            if (p) {
                *p = '/';
                if (newuri.len > 0) {
                    ngx_memcpy(p + 1, newuri.data, newuri.len);
                }
                newuri.data = p;
                newuri.len++;
            }
        }
    }

    ctx->uri = newuri;
    ngx_str_null(&ctx->redirect_url);
    ctx->redirect_code = 0;

    return RULE_RC_MATCH;
}

static void
ngx_rewrite_save_skip_flag(void *cln)
{
    /* cleanup handler - не используется, просто метка */
}

ngx_int_t
ngx_rewrite_set_skip_after_redirect(ngx_http_request_t *r)
{
    ngx_pool_cleanup_t     *cln;
    ngx_rewrite_pool_ctx_t  *pctx = NULL;
    for (cln = r->pool->cleanup; cln; cln = cln->next) {
        if (cln->handler == ngx_rewrite_save_skip_flag) {
            pctx = cln->data;
            break;
        }
    }

    if (!pctx) {
    cln = ngx_pool_cleanup_add(r->pool, sizeof(ngx_rewrite_pool_ctx_t));
        if (cln == NULL) {
            return NGX_ERROR;
        }
        pctx = cln->data;
    }

    /* Сохраняем флаг и текущий URI */
    pctx->skip_rewrite_after_redirect = 1;

    cln->handler = ngx_rewrite_save_skip_flag; /* просто метка */
    return NGX_OK;
}


/*
 * Apply a complete list of rewrite rules.
 * Port of Apache apply_rewrite_list() (mod_rewrite.c:4647-4817).
 *
 * Returns: 0 = no change, ACTION_NORMAL/ACTION_NOESCAPE = changed,
 *          ACTION_STATUS = status set.
 */
ngx_int_t
ngx_rewrite_apply_list(ngx_http_request_t *r, ngx_array_t *rules,
    ngx_str_t *htaccess, ngx_str_t baseurl, ngx_int_t baseurl_set)
{
    ngx_rewrite_rule_t    *entries;
    ngx_rewrite_rule_t    *p;
    ngx_uint_t             i;
    ngx_int_t              changed = 0;
    ngx_rewrite_rule_rc_e  rc;
    ngx_int_t              round = 1;
    ngx_rewrite_ctx_t     *ctx;
    ngx_pool_cleanup_t     *cln;
    ngx_rewrite_pool_ctx_t *pctx;

    if (r->internal) {
        for (cln = r->pool->cleanup; cln; cln = cln->next) {
            if (cln->handler == ngx_rewrite_save_skip_flag) {
                pctx = cln->data;
                if (pctx && pctx->skip_rewrite_after_redirect) {
                    pctx->skip_rewrite_after_redirect = 0;
                    /* Skip rewriting on redirected URI */
                    return 0;
                }
            }
        }
    }

    if (rules == NULL || rules->nelts == 0) {
        return 0;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_apache_rewrite_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_rewrite_ctx_t));
        if (ctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_apache_rewrite_module);
    }

    /* Check END flag from previous invocation */
    if (ctx->end) {
        return 0;
    }

    ctx->r = r;
    ctx->uri = r->uri;
    ctx->match_uri = r->uri;
    ctx->uri_changed = 0;

    ctx->htaccess = ((htaccess) ? (*htaccess) : (ngx_str_t)ngx_null_string);

    if (ctx->htaccess.data && ctx->match_uri.len > 1 && ctx->match_uri.data[0] == '/') {
        ngx_str_t tmp_str = ngx_null_string;
        // Allocate buffer for relative URI (strip htaccess directory prefix)
        if (r->uri.len > (size_t)(ctx->htaccess.len + 1)) {
            tmp_str.data = ngx_pnalloc(r->pool, r->uri.len - ctx->htaccess.len + 1);
            if (tmp_str.data != NULL) {
                // Strip the htaccess directory prefix to get relative URI for pattern matching
                // Example: /wordpress/wp-json/ -> /wp-json/ when htaccess=/wordpress
                ngx_memcpy(tmp_str.data, r->uri.data + ctx->htaccess.len + 1,
                           r->uri.len - ctx->htaccess.len - 1);
                tmp_str.len = r->uri.len - (ctx->htaccess.len + 1);
                ctx->match_uri = tmp_str;
            } else {
                // Fallback: use original URI on allocation failure
                ctx->match_uri = r->uri;
            }
        }
    } else {
        // Use original URI for matching when htaccess not applicable
        ctx->match_uri = r->uri;
    }
    ngx_str_null(&ctx->redirect_url);
    ctx->redirect_code = 0;

    entries = rules->elts;

loop:
    for (i = 0; i < rules->nelts; i++) {
        p = &entries[i];

        /* Skip on subrequests if NS flag or redirect */
        if (r->main != r
            && (p->flags & (RULEFLAG_IGNOREONSUBREQ | RULEFLAG_FORCEREDIRECT)))
        {
            continue;
        }

        /* Apply this rule */
        rc = ngx_rewrite_apply_rule(p, ctx, baseurl, baseurl_set);

        if (rc != RULE_RC_NOMATCH) {
            /* Check for status set */
            if (rc == RULE_RC_STATUS_SET) {
                return ACTION_STATUS_SET;
            }

            /* Status flag [F], [G], or [R=NNN] with nosub */
            if (p->flags & RULEFLAG_STATUS) {
                ctx->status_code = p->forced_responsecode;
                return ACTION_STATUS;
            }

            /* Track changes for non-nosub rules */
            if (rc != RULE_RC_NOSUB) {
                changed = (p->flags & RULEFLAG_NOESCAPE)
                          ? ACTION_NOESCAPE : ACTION_NORMAL;
            }

            /* PassThrough [PT] — let other handlers process */
            if (p->flags & RULEFLAG_PASSTHROUGH) {
                r->uri = ctx->uri;
                ctx->uri_changed = 1;
                changed = ACTION_NORMAL;
                break;
            }

            /* END flag */
            if (p->flags & RULEFLAG_END) {
                ctx->end = 1;
                break;
            }

            /* Last rule [L] or proxy [P] */
            if (p->flags & (RULEFLAG_LASTRULE | RULEFLAG_PROXY)) {
                break;
            }

            /* New round [N] */
            if (p->flags & RULEFLAG_NEWROUND) {
                ngx_int_t maxrounds = p->maxrounds;
                if (maxrounds == 0) {
                    maxrounds = NGX_REWRITE_MAX_ROUNDS;
                }

                if (++round >= maxrounds) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "mod_rewrite: RewriteRule \"%V\" and URI \"%V\" "
                        "exceeded maximum number of rounds (%d) "
                        "via the [N] flag",
                        &p->pattern, &r->uri, maxrounds);
                    return ACTION_STATUS;
                }

                /* Update r->uri for next round */
                r->uri = ctx->uri;
                ctx->uri_changed = 1;
                goto loop;
            }

            /* Skip [S=N] */
            if (p->skip > 0) {
                ngx_int_t s = p->skip;
                while (i < rules->nelts && s > 0) {
                    i++;
                    s--;
                }
            }

            /* Chain [C] — continue to next rule */
        } else {
            /* No match: if chained, skip rest of chain */
            while (i < rules->nelts && (p->flags & RULEFLAG_CHAIN)) {
                i++;
                if (i < rules->nelts) {
                    p = &entries[i];
                }
            }
        }
    }

    /* Update r->uri if changed */
    if (changed && ctx->uri.len > 0 && ctx->redirect_url.len == 0) {
        r->uri = ctx->uri;
        ctx->uri_changed = 1;
    }

    return changed;
}
