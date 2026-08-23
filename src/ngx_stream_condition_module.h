#ifndef _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_
#define _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#include "ngx_condition.h"


#define NGX_STREAM_MAIN_WHEN_CONF  0x00100000
#define NGX_STREAM_SRV_WHEN_CONF   0x00200000


typedef struct {
    ngx_stream_complex_value_t  *value;
    ngx_condition_expr_id_t      expr_id;
} ngx_stream_condition_complex_value_ctx_t;


extern ngx_module_t  ngx_stream_condition_module;


ngx_uint_t ngx_stream_condition_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_stream_condition_from_when_cmd_type(ngx_uint_t type);

ngx_int_t ngx_stream_condition_get_expr_result(ngx_stream_session_t *s,
    ngx_condition_expr_id_t expr_id);

char *ngx_stream_set_conditional_complex_value_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_stream_set_conditional_complex_value_zero_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_stream_set_conditional_complex_value_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

ngx_int_t ngx_stream_get_conditional_complex_value(ngx_stream_session_t *s,
    ngx_array_t *values, ngx_str_t *value);
size_t ngx_stream_get_conditional_complex_value_size(ngx_stream_session_t *s,
    ngx_array_t *values, size_t default_value);


static ngx_inline ngx_int_t
ngx_stream_condition_eval_expr(void *data, ngx_condition_expr_id_t expr_id)
{
    return ngx_stream_condition_get_expr_result(data, expr_id);
}


static ngx_inline ngx_flag_t
ngx_stream_get_conditional_flag_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_flag_ctx_t   *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_flag_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_flag_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_int_t
ngx_stream_get_conditional_num_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_num_ctx_t   *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_num_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_num_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline size_t
ngx_stream_get_conditional_size_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_size_ctx_t   *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_size_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_size_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_SIZE;
}


static ngx_inline off_t
ngx_stream_get_conditional_off_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_off_ctx_t   *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_off_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_off_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_msec_t
ngx_stream_get_conditional_msec_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_msec_ctx_t   *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_msec_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_msec_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_MSEC;
}


static ngx_inline time_t
ngx_stream_get_conditional_sec_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_sec_ctx_t   *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_sec_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_sec_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET;
}


static ngx_inline ngx_uint_t
ngx_stream_get_conditional_enum_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_enum_ctx_t   *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_enum_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_enum_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NGX_CONF_UNSET_UINT;
}


static ngx_inline ngx_uint_t
ngx_stream_get_conditional_bitmask_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_bitmask_ctx_t   *ctx;
    size_t                              element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_bitmask_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_bitmask_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : 0;
}


static ngx_inline void *
ngx_stream_get_conditional_ptr_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_ptr_ctx_t   *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_ptr_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_ptr_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_array_t *
ngx_stream_get_conditional_str_array_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_str_array_ctx_t   *ctx;
    size_t                                element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_str_array_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_str_array_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_array_t *
ngx_stream_get_conditional_keyval_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_keyval_ctx_t   *ctx;
    size_t                             element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_keyval_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_keyval_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? ctx->value : NULL;
}


static ngx_inline ngx_str_t *
ngx_stream_get_conditional_str_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_str_ctx_t   *ctx;
    size_t                          element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_str_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_str_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? &ctx->value : NULL;
}


static ngx_inline ngx_bufs_t *
ngx_stream_get_conditional_bufs_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_bufs_ctx_t   *ctx;
    size_t                           element_size, expr_id_offset;

    element_size = sizeof(ngx_conf_condition_bufs_ctx_t);
    expr_id_offset = offsetof(ngx_conf_condition_bufs_ctx_t, expr_id);

    ctx = ngx_conf_get_conditional_ctx(s, values, element_size, expr_id_offset,
                                       ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? &ctx->value : NULL;
}


#endif /* _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_ */
