
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_expr_module.h"


typedef struct {
    ngx_array_t  *msec;
    ngx_array_t  *sec;
    ngx_str_t     next_args;
} ngx_http_expr_time_test_conf_t;


static ngx_int_t ngx_http_expr_time_test_handler(ngx_http_request_t *r);
static char *ngx_http_expr_time_test(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_expr_time_test_create_conf(ngx_conf_t *cf);
static char *ngx_http_expr_time_test_merge_conf(ngx_conf_t *cf,
    void *parent, void *child);


static ngx_command_t  ngx_http_expr_time_test_commands[] = {

    { ngx_string("expr_time_msec"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
                       |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                       |NGX_HTTP_LOC_WHEN_CONF|NGX_CONF_TAKE1,
      ngx_http_set_expr_complex_value_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_expr_time_test_conf_t, msec),
      NULL },

    { ngx_string("expr_time_sec"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
                       |NGX_HTTP_MAIN_WHEN_CONF|NGX_HTTP_SRV_WHEN_CONF
                       |NGX_HTTP_LOC_WHEN_CONF|NGX_CONF_TAKE1,
      ngx_http_set_expr_complex_value_sec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_expr_time_test_conf_t, sec),
      NULL },

    { ngx_string("expr_time_args"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_expr_time_test_conf_t, next_args),
      NULL },

    { ngx_string("expr_time_test"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_expr_time_test,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_expr_time_test_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_expr_time_test_create_conf,   /* create location configuration */
    ngx_http_expr_time_test_merge_conf     /* merge location configuration */
};


ngx_module_t  ngx_http_expr_time_test_module = {
    NGX_MODULE_V1,
    &ngx_http_expr_time_test_module_ctx,   /* module context */
    ngx_http_expr_time_test_commands,      /* module directives */
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


static ngx_int_t
ngx_http_expr_time_test_handler(ngx_http_request_t *r)
{
    time_t                           sec;
    ngx_int_t                        rc;
    ngx_msec_t                       msec;
    ngx_buf_t                       *b;
    ngx_str_t                        args;
    ngx_chain_t                      out;
    ngx_http_expr_time_test_conf_t  *conf;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    conf = ngx_http_get_module_loc_conf(r, ngx_http_expr_time_test_module);

    b = ngx_create_temp_buf(r->pool, 4 * NGX_INT64_LEN + 4);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    msec = ngx_http_get_expr_complex_value_msec(r, conf->msec, 17);
    sec = ngx_http_get_expr_complex_value_sec(r, conf->sec, 19);
    b->last = ngx_sprintf(b->last, "%M|%T", msec, sec);

    if (conf->next_args.len) {
        args = r->args;
        r->args = conf->next_args;

        msec = ngx_http_get_expr_complex_value_msec(r, conf->msec, 17);
        sec = ngx_http_get_expr_complex_value_sec(r, conf->sec, 19);
        b->last = ngx_sprintf(b->last, "|%M|%T", msec, sec);

        r->args = args;
    }

    *b->last++ = LF;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    b->last_buf = (r == r->main);
    b->last_in_chain = 1;
    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static char *
ngx_http_expr_time_test(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_expr_time_test_handler;

    return NGX_CONF_OK;
}


static void *
ngx_http_expr_time_test_create_conf(ngx_conf_t *cf)
{
    ngx_http_expr_time_test_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_expr_time_test_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static char *
ngx_http_expr_time_test_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_expr_time_test_conf_t  *prev = parent;
    ngx_http_expr_time_test_conf_t  *conf = child;

    if (ngx_conf_merge_expr_ptr_value(cf, &conf->msec, prev->msec, NULL)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_merge_expr_ptr_value(cf, &conf->sec, prev->sec, NULL)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_str_value(conf->next_args, prev->next_args, "");

    return NGX_CONF_OK;
}
