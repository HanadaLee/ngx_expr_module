
/*
 * Copyright (C) Hanada
 */


#ifndef _NGX_HTTP_EXPR_MODULE_H_INCLUDED_
#define _NGX_HTTP_EXPR_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_expr.h"


#define NGX_HTTP_MAIN_WHEN_CONF  0x00100000
#define NGX_HTTP_SRV_WHEN_CONF   0x00200000
#define NGX_HTTP_LOC_WHEN_CONF   0x00400000


typedef struct {
    ngx_http_complex_value_t  *value;
    ngx_expr_when_id_t        expr_id;
} ngx_http_expr_complex_value_ctx_t;


extern ngx_module_t  ngx_http_expr_module;


ngx_uint_t ngx_http_expr_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_http_expr_from_when_cmd_type(ngx_uint_t type);

ngx_int_t ngx_http_expr_get_result(ngx_http_request_t *r,
    ngx_expr_when_id_t expr_id);

char *ngx_http_set_expr_complex_value_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_http_set_expr_complex_value_zero_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_http_set_expr_complex_value_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

#if (NGX_RESTY_EXT)
char *ngx_http_set_expr_complex_value_msec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_http_set_expr_complex_value_sec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
ngx_msec_t ngx_http_get_expr_complex_value_msec(
    ngx_http_request_t *r, ngx_array_t *values, ngx_msec_t default_value);
time_t ngx_http_get_expr_complex_value_sec(ngx_http_request_t *r,
    ngx_array_t *values, time_t default_value);
#endif

ngx_int_t ngx_http_get_expr_complex_value(ngx_http_request_t *r,
    ngx_array_t *values, ngx_str_t *value);
size_t ngx_http_get_expr_complex_value_size(ngx_http_request_t *r,
    ngx_array_t *values, size_t default_value);


static ngx_inline ngx_int_t
ngx_http_expr_eval(void *data, ngx_expr_when_id_t expr_id)
{
    return ngx_http_expr_get_result(data, expr_id);
}


static ngx_inline ngx_flag_t
ngx_http_get_expr_flag_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_flag_ctx_t         *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_flag_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_flag_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_int_t
ngx_http_get_expr_num_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_num_ctx_t         *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_num_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_num_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline size_t
ngx_http_get_expr_size_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_size_ctx_t         *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_size_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_size_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_SIZE;
}


static ngx_inline off_t
ngx_http_get_expr_off_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_off_ctx_t         *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_off_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_off_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_msec_t
ngx_http_get_expr_msec_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_msec_ctx_t         *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_msec_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_msec_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_MSEC;
}


static ngx_inline time_t
ngx_http_get_expr_sec_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_sec_ctx_t         *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_sec_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_sec_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_uint_t
ngx_http_get_expr_enum_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_enum_ctx_t         *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_enum_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_enum_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_UINT;
}


static ngx_inline ngx_uint_t
ngx_http_get_expr_bitmask_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_bitmask_ctx_t         *ctx;
    size_t                              element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bitmask_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : 0;
}


static ngx_inline void *
ngx_http_get_expr_ptr_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_ptr_ctx_t         *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_ptr_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_ptr_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_array_t *
ngx_http_get_expr_str_array_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_str_array_ctx_t         *ctx;
    size_t                                element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_array_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_str_array_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_array_t *
ngx_http_get_expr_keyval_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_keyval_ctx_t         *ctx;
    size_t                             element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_keyval_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_keyval_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_str_t *
ngx_http_get_expr_str_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_str_ctx_t         *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_str_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? &ctx->value : NULL;
}


static ngx_inline ngx_bufs_t *
ngx_http_get_expr_bufs_value(ngx_http_request_t *r, ngx_array_t *values)
{
    ngx_conf_expr_bufs_ctx_t         *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bufs_ctx_t);
    expr_id_offset = offsetof(ngx_conf_expr_bufs_ctx_t, expr_id);

    ctx = ngx_conf_get_expr_ctx(r, values, element_size, expr_id_offset,
                                ngx_http_expr_eval);

    return (ctx != NULL) ? &ctx->value : NULL;
}


#endif /* _NGX_HTTP_EXPR_MODULE_H_INCLUDED_ */
