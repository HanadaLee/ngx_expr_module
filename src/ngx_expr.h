
/*
 * Copyright (C) Hanada
 */


#ifndef _NGX_EXPR_H_INCLUDED_
#define _NGX_EXPR_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_EXPR_NAME_MAX_LEN       288

#define NGX_EXPR_NO_ID              (ngx_expr_id_t) -1
#define NGX_EXPR_NO_WHEN_ID         (ngx_expr_when_id_t) -1

#define NGX_EXPR_WHEN_MISS          0
#define NGX_EXPR_WHEN_HIT           1

#define NGX_EXPR_NO_ARGS            0
#define NGX_EXPR_MAX_ARGS           (ngx_uint_t) -1
#define NGX_EXPR_NO_IGNORE_CASE     0
#define NGX_EXPR_ALLOW_IGNORE_CASE  1

#define NGX_EXPR_FUNC_LOGIC_FIRST   NGX_EXPR_FUNC_NOT
#define NGX_EXPR_FUNC_LOGIC_LAST    NGX_EXPR_FUNC_OR

#define NGX_EXPR_IP_KEY_LEN         16


typedef ngx_uint_t  ngx_expr_id_t;
typedef ngx_uint_t  ngx_expr_when_id_t;


typedef enum {
    NGX_EXPR_FUNC_NOT = 0,
    NGX_EXPR_FUNC_AND,
    NGX_EXPR_FUNC_OR,
    NGX_EXPR_FUNC_BOOL,
    NGX_EXPR_FUNC_IS_EMPTY,
    NGX_EXPR_FUNC_STR_EQ,
    NGX_EXPR_FUNC_STR_STARTS_WITH,
    NGX_EXPR_FUNC_STR_ENDS_WITH,
    NGX_EXPR_FUNC_STR_CONTAINS,
    NGX_EXPR_FUNC_STR_REGEX_MATCH,
    NGX_EXPR_FUNC_STR_IN,
    NGX_EXPR_FUNC_IS_NUM,
    NGX_EXPR_FUNC_NUM_EQ,
    NGX_EXPR_FUNC_NUM_LT,
    NGX_EXPR_FUNC_NUM_LE,
    NGX_EXPR_FUNC_NUM_GT,
    NGX_EXPR_FUNC_NUM_GE,
    NGX_EXPR_FUNC_NUM_RANGE,
    NGX_EXPR_FUNC_NUM_IN,
    NGX_EXPR_FUNC_TIME_RANGE,
    NGX_EXPR_FUNC_IS_IP,
    NGX_EXPR_FUNC_IS_CIDR,
    NGX_EXPR_FUNC_IP_RANGE,
#if (nginx_version >= 1031005 || NGX_CJSON)
    NGX_EXPR_FUNC_IS_JSON,
#endif
    NGX_EXPR_FUNC_INVALID
} ngx_expr_func_e;


typedef struct {
    ngx_str_t                name;
    ngx_expr_id_t            id;
    unsigned                 defined:1;
} ngx_expr_name_t;


typedef struct {
    ngx_expr_id_t            expr_id;
    unsigned                 negative:1;
} ngx_expr_term_t;


typedef struct {
    unsigned                 set:1;
    ngx_int_t                start;
    ngx_int_t                end;
} ngx_expr_range_t;


typedef struct {
    ngx_expr_when_id_t       expr_id;
    ngx_array_t              terms;       /* ngx_expr_term_t */
} ngx_expr_when_t;


typedef struct {
    ngx_array_t              names;       /* ngx_expr_name_t */
    ngx_array_t              expressions; /* ngx_expr_when_t */
} ngx_expr_registry_t;


typedef struct {
    sa_family_t              family;
    in_addr_t                in;
#if (NGX_HAVE_INET6)
    u_char                   in6[16];
#endif
} ngx_expr_ip_t;


typedef struct {
    sa_family_t              family;
    unsigned                 range:1;
    in_addr_t                addr;
    in_addr_t                mask;
    in_addr_t                start;
    in_addr_t                end;
#if (NGX_HAVE_INET6)
    u_char                   addr6[16];
    u_char                   mask6[16];
#endif
} ngx_expr_ip_item_t;


typedef struct ngx_expr_ip_ranges_s  ngx_expr_ip_ranges_t;


typedef struct {
    ngx_flag_t               value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_flag_ctx_t;


typedef struct {
    ngx_str_t                value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_str_ctx_t;


typedef struct {
    void                    *value;
    ngx_expr_when_id_t      expr_id;
} ngx_conf_expr_ptr_ctx_t;


typedef struct {
    ngx_array_t             *value;
    ngx_expr_when_id_t      expr_id;
} ngx_conf_expr_str_array_ctx_t;


typedef struct {
    ngx_array_t             *value;
    ngx_expr_when_id_t      expr_id;
} ngx_conf_expr_keyval_ctx_t;


typedef struct {
    ngx_int_t                value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_num_ctx_t;


typedef struct {
    size_t                   value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_size_ctx_t;


typedef struct {
    off_t                    value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_off_ctx_t;


typedef struct {
    ngx_msec_t               value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_msec_ctx_t;


typedef struct {
    time_t                   value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_sec_ctx_t;


typedef struct {
    ngx_bufs_t               value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_bufs_ctx_t;


typedef struct {
    ngx_uint_t               value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_enum_ctx_t;


typedef struct {
    ngx_uint_t               value;
    ngx_expr_when_id_t       expr_id;
} ngx_conf_expr_bitmask_ctx_t;


typedef ngx_int_t (*ngx_expr_eval_pt)(void *data, ngx_expr_when_id_t expr_id);


ngx_int_t ngx_expr_registry_init(ngx_pool_t *pool,
    ngx_expr_registry_t *registry);
ngx_expr_name_t *ngx_expr_get_name(ngx_conf_t *cf,
    ngx_expr_registry_t *registry, ngx_str_t *name);
ngx_int_t ngx_expr_parse_terms(ngx_conf_t *cf,
    ngx_expr_registry_t *registry, ngx_uint_t first,
    ngx_array_t *terms);
ngx_expr_when_id_t ngx_expr_get_when_id(ngx_conf_t *cf,
    ngx_expr_registry_t *registry, ngx_array_t *terms);
ngx_int_t ngx_expr_validate_names(ngx_conf_t *cf,
    ngx_expr_registry_t *registry);

ngx_int_t ngx_expr_str_eq(ngx_str_t *a, ngx_str_t *b, ngx_uint_t ignore_case);
ngx_int_t ngx_expr_str_starts_with(ngx_str_t *value, ngx_str_t *prefix,
    ngx_uint_t ignore_case);
ngx_int_t ngx_expr_str_ends_with(ngx_str_t *value, ngx_str_t *suffix,
    ngx_uint_t ignore_case);
ngx_int_t ngx_expr_str_contains(ngx_str_t *value, ngx_str_t *part,
    ngx_uint_t ignore_case);
ngx_int_t ngx_expr_is_number(ngx_str_t *value);
ngx_int_t ngx_expr_compare_numbers(ngx_str_t *a, ngx_str_t *b,
    ngx_int_t *result);
ngx_int_t ngx_expr_parse_uint_range(ngx_str_t *value,
    ngx_int_t *start, ngx_int_t *end);
ngx_int_t ngx_expr_set_time_range(ngx_conf_t *cf,
    ngx_expr_range_t *range, ngx_str_t *value,
    ngx_int_t minimum, ngx_int_t maximum, const char *field);
ngx_int_t ngx_expr_parse_timezone(ngx_conf_t *cf, ngx_str_t *value,
    ngx_int_t *gmt_offset);
ngx_int_t ngx_expr_range_matches(ngx_expr_range_t *range, ngx_int_t value);
ngx_int_t ngx_expr_parse_ip(ngx_str_t *value, ngx_expr_ip_t *ip);
ngx_int_t ngx_expr_is_cidr(ngx_str_t *value);
ngx_int_t ngx_expr_parse_ip_item(ngx_str_t *value, ngx_expr_ip_item_t *item);
ngx_int_t ngx_expr_parse_ip_items(ngx_conf_t *cf, ngx_uint_t first,
    ngx_array_t **items);
ngx_int_t ngx_expr_parse_ip_ranges(ngx_conf_t *cf, ngx_uint_t first,
    ngx_expr_ip_ranges_t **ranges);
ngx_int_t ngx_expr_ip_item_matches(ngx_expr_ip_t *ip, ngx_expr_ip_item_t *item);
ngx_int_t ngx_expr_ip_ranges_match(ngx_expr_ip_t *ip,
    ngx_expr_ip_ranges_t *ranges);
#if (nginx_version >= 1031005 || NGX_CJSON)
ngx_int_t ngx_expr_is_json(ngx_pool_t *pool, ngx_str_t *value);
#endif

ngx_expr_when_id_t ngx_expr_get_current_when_id(void);
void ngx_expr_set_current_when_id(ngx_expr_when_id_t expr_id);
ngx_expr_when_id_t ngx_expr_get_associated_when_id(ngx_conf_t *cf);

void *ngx_expr_find_ctx(ngx_array_t *values,
    ngx_expr_when_id_t expr_id, size_t element_size,
    size_t expr_id_offset);
void *ngx_conf_get_expr_ctx(void *data, ngx_array_t *values,
                            size_t element_size, size_t expr_id_offset,
    ngx_expr_eval_pt eval);

ngx_int_t ngx_conf_init_expr_flag_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_flag_t default_value);
ngx_int_t ngx_conf_merge_expr_flag_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_flag_t default_value);
ngx_int_t ngx_conf_init_expr_str_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_str_t default_value);
ngx_int_t ngx_conf_merge_expr_str_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_str_t default_value);
ngx_int_t ngx_conf_init_expr_ptr_value(ngx_conf_t *cf,
    ngx_array_t **values, void *default_value);
ngx_int_t ngx_conf_merge_expr_ptr_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, void *default_value);
ngx_int_t ngx_conf_init_expr_num_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_int_t default_value);
ngx_int_t ngx_conf_merge_expr_num_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_int_t default_value);
ngx_int_t ngx_conf_init_expr_size_value(ngx_conf_t *cf,
    ngx_array_t **values, size_t default_value);
ngx_int_t ngx_conf_merge_expr_size_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, size_t default_value);
ngx_int_t ngx_conf_init_expr_off_value(ngx_conf_t *cf,
    ngx_array_t **values, off_t default_value);
ngx_int_t ngx_conf_merge_expr_off_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, off_t default_value);
ngx_int_t ngx_conf_init_expr_msec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_msec_t default_value);
ngx_int_t ngx_conf_merge_expr_msec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_msec_t default_value);
ngx_int_t ngx_conf_init_expr_sec_value(ngx_conf_t *cf,
    ngx_array_t **values, time_t default_value);
ngx_int_t ngx_conf_merge_expr_sec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, time_t default_value);
ngx_int_t ngx_conf_init_expr_bufs_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_num, size_t default_size);
ngx_int_t ngx_conf_merge_expr_bufs_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_num,
    size_t default_size);
ngx_int_t ngx_conf_init_expr_enum_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_value);
ngx_int_t ngx_conf_merge_expr_enum_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_value);
ngx_int_t ngx_conf_init_expr_bitmask_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_value);
ngx_int_t ngx_conf_merge_expr_bitmask_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_value);

char *ngx_expr_call_ptr_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf, size_t element_size,
    size_t value_offset, size_t expr_id_offset,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *));
char *ngx_conf_set_conditional_flag_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_str_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_str_array_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_keyval_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_num_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_off_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_msec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_sec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_bufs_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_enum_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
char *ngx_conf_set_conditional_bitmask_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);


#endif /* _NGX_EXPR_H_INCLUDED_ */
