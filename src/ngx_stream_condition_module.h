#ifndef _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_
#define _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#include "ngx_condition.h"


#if (NGX_CONDITION)


#define NGX_STREAM_MAIN_WHEN_CONF  0x00100000
#define NGX_STREAM_SRV_WHEN_CONF   0x00200000

#define NGX_STREAM_ANY_WHEN_CONF                                        \
    (NGX_STREAM_MAIN_WHEN_CONF | NGX_STREAM_SRV_WHEN_CONF)


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


#define ngx_stream_condition_scalar_getter(name, type, ctx_type, unset)      \
    static ngx_inline type                                                   \
    name(ngx_stream_session_t *s, ngx_array_t *values)                       \
    {                                                                        \
        ctx_type *ctx;                                                       \
        ctx = ngx_conf_get_conditional_ctx(s, values, sizeof(ctx_type),      \
                  offsetof(ctx_type, expr_id),                               \
                  ngx_stream_condition_eval_expr);                           \
        return (ctx != NULL) ? ctx->value : (unset);                         \
    }

ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_flag_value,
    ngx_flag_t, ngx_conf_condition_flag_ctx_t, NGX_CONF_UNSET)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_num_value,
    ngx_int_t, ngx_conf_condition_num_ctx_t, NGX_CONF_UNSET)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_size_value,
    size_t, ngx_conf_condition_size_ctx_t, NGX_CONF_UNSET_SIZE)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_off_value,
    off_t, ngx_conf_condition_off_ctx_t, NGX_CONF_UNSET)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_msec_value,
    ngx_msec_t, ngx_conf_condition_msec_ctx_t, NGX_CONF_UNSET_MSEC)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_sec_value,
    time_t, ngx_conf_condition_sec_ctx_t, NGX_CONF_UNSET)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_enum_value,
    ngx_uint_t, ngx_conf_condition_enum_ctx_t, NGX_CONF_UNSET_UINT)
ngx_stream_condition_scalar_getter(ngx_stream_get_conditional_bitmask_value,
    ngx_uint_t, ngx_conf_condition_bitmask_ctx_t, 0)


#define ngx_stream_condition_pointer_getter(name, type, ctx_type)           \
    static ngx_inline type                                                   \
    name(ngx_stream_session_t *s, ngx_array_t *values)                       \
    {                                                                        \
        ctx_type *ctx;                                                       \
        ctx = ngx_conf_get_conditional_ctx(s, values, sizeof(ctx_type),      \
                  offsetof(ctx_type, expr_id),                               \
                  ngx_stream_condition_eval_expr);                           \
        return (ctx != NULL) ? ctx->value : NULL;                            \
    }

ngx_stream_condition_pointer_getter(ngx_stream_get_conditional_str_array_value,
    ngx_array_t *, ngx_conf_condition_str_array_ctx_t)
ngx_stream_condition_pointer_getter(ngx_stream_get_conditional_keyval_value,
    ngx_array_t *, ngx_conf_condition_keyval_ctx_t)


static ngx_inline ngx_str_t *
ngx_stream_get_conditional_str_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_str_ctx_t *ctx;

    ctx = ngx_conf_get_conditional_ctx(s, values,
              sizeof(ngx_conf_condition_str_ctx_t),
              offsetof(ngx_conf_condition_str_ctx_t, expr_id),
              ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? &ctx->value : NULL;
}


static ngx_inline ngx_bufs_t *
ngx_stream_get_conditional_bufs_value(ngx_stream_session_t *s,
    ngx_array_t *values)
{
    ngx_conf_condition_bufs_ctx_t *ctx;

    ctx = ngx_conf_get_conditional_ctx(s, values,
              sizeof(ngx_conf_condition_bufs_ctx_t),
              offsetof(ngx_conf_condition_bufs_ctx_t, expr_id),
              ngx_stream_condition_eval_expr);

    return (ctx != NULL) ? &ctx->value : NULL;
}


#endif /* NGX_CONDITION */


#endif /* _NGX_STREAM_CONDITION_MODULE_H_INCLUDED_ */
