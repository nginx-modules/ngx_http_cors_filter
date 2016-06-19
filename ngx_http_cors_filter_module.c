#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


static ngx_int_t ngx_http_cors_filter_init(ngx_conf_t *cf);
static void *ngx_http_cors_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_http_cors_header_filter(ngx_http_request_t *r);
static char * ngx_http_cors_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_cors_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);


typedef struct {
    ngx_array_t *cors;
} ngx_http_cors_loc_conf_t;


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;


static ngx_http_module_t  ngx_http_cors_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_cors_filter_init,             /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_cors_create_loc_conf,         /* create location configuration */
    ngx_http_cors_merge_loc_conf,          /* merge location configuration */
};

static ngx_command_t ngx_http_cors_filter_commands[] = {
    {
        ngx_string("cors"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_cors_add,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};


ngx_module_t  ngx_http_cors_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_cors_filter_module_ctx,      /* module context */
    ngx_http_cors_filter_commands,         /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_cors_add(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_cors_loc_conf_t *hclf = conf;

    u_char               errstr[NGX_MAX_CONF_ERRSTR];
    ngx_str_t           *value;
    ngx_regex_elt_t     *re;
    ngx_regex_compile_t rc;

    if (hclf->cors == NULL) {
        hclf->cors = ngx_array_create(cf->pool, 1, sizeof(ngx_regex_elt_t));
        if (hclf->cors == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    value = cf->args->elts;
    rc.pattern.len = value[1].len;
    rc.pattern.data = value[1].data;
    rc.pool = cf->pool;
    rc.options = 0;
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

#if (NGX_PCRE)
    if (ngx_regex_compile(&rc) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0, "[cors]: compile regex error");
        return NGX_CONF_ERROR;
    }
#else
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
     "the using of the regex cors requires PCRE library");

    return NGX_ERROR;
#endif

    re = ngx_array_push(hclf->cors);
    if (re == NULL) {
        return NGX_CONF_ERROR;
    }
    re->regex = rc.regex;
    re->name = value[1].data;

    return NGX_CONF_OK;
}


static void *
ngx_http_cors_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_cors_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_cors_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static char *
ngx_http_cors_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_cors_loc_conf_t *prev = parent;
    ngx_http_cors_loc_conf_t *conf = child;

    if (conf->cors == NULL) {
        conf->cors = prev->cors;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_cors_header_filter(ngx_http_request_t *r)
{
    ngx_list_part_t      *part = &r->headers_in.headers.part;
    ngx_table_elt_t      *header = part->elts;

    ngx_regex_elt_t *re;
    ngx_str_t            *value;
    ngx_uint_t            i;
    ngx_table_elt_t      *h;
    ngx_http_cors_loc_conf_t *hclf;

    hclf = ngx_http_get_module_loc_conf(r, ngx_http_cors_filter_module);
    if (hclf->cors == NULL || hclf->cors->nelts == 0) {
        return ngx_http_next_header_filter(r);
    }

    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            header = part->elts;
            i = 0;
        }
        if (header[i].hash == 0) {
            continue;
        }

        if (0 == ngx_strncasecmp(header[i].key.data,
                (u_char *) "origin",
                header[i].key.len
            ))
        {
            goto found;
        }
    }

    return ngx_http_next_header_filter(r);

found:
    re = hclf->cors->elts;

    if (ngx_regex_exec_array(hclf->cors, &header[i].value, r->connection->log) != NGX_OK) {
        return ngx_http_next_header_filter(r);
    }

    value = hclf->cors->elts;
    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    /* tips
        we add two headers whether response header already have this header
        make sure response should do not add two headers as following
    */
    h->hash = 1;
    ngx_str_set(&h->key, "Access-Control-Allow-Credentials");
    ngx_str_set(&h->value, "true");

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    ngx_str_set(&h->key, "Access-Control-Allow-Origin");
    h->value.len = header[i].value.len;
    h->value.data = header[i].value.data;

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_cors_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_cors_header_filter;

    return NGX_OK;
}