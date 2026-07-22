#ifndef _NGX_CONDITION_H_INCLUDED_
#define _NGX_CONDITION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_condition_id_t;
typedef ngx_uint_t  ngx_condition_expr_id_t;


#define NGX_CONDITION_NAME_MAX_LEN  288

#define NGX_CONDITION_NO_ID       ((ngx_condition_id_t) -1)
#define NGX_CONDITION_NO_EXPR_ID  ((ngx_condition_expr_id_t) -1)

#define NGX_CONDITION_EXPR_MISS  0
#define NGX_CONDITION_EXPR_HIT   1


#define NGX_CONDITION_OP_LOGIC_FIRST  NGX_CONDITION_OP_NOT
#define NGX_CONDITION_OP_LOGIC_LAST   NGX_CONDITION_OP_OR


typedef enum {
    NGX_CONDITION_OP_NOT = 0,
    NGX_CONDITION_OP_AND,
    NGX_CONDITION_OP_OR,
    NGX_CONDITION_OP_IS_EMPTY,
    NGX_CONDITION_OP_IS_NOT_EMPTY,
    NGX_CONDITION_OP_STR_EQ,
    NGX_CONDITION_OP_STR_NE,
    NGX_CONDITION_OP_STR_STARTS_WITH,
    NGX_CONDITION_OP_STR_ENDS_WITH,
    NGX_CONDITION_OP_STR_CONTAINS,
    NGX_CONDITION_OP_STR_REGEX_MATCH,
    NGX_CONDITION_OP_IS_NUM,
    NGX_CONDITION_OP_NUM_EQ,
    NGX_CONDITION_OP_NUM_NE,
    NGX_CONDITION_OP_NUM_LT,
    NGX_CONDITION_OP_NUM_LE,
    NGX_CONDITION_OP_NUM_GT,
    NGX_CONDITION_OP_NUM_GE,
    NGX_CONDITION_OP_NUM_RANGE,
    NGX_CONDITION_OP_TIME_RANGE,
    NGX_CONDITION_OP_IS_IP,
    NGX_CONDITION_OP_IS_CIDR,
    NGX_CONDITION_OP_IP_RANGE,
#if (NGX_CJSON)
    NGX_CONDITION_OP_IS_JSON,
#endif
    NGX_CONDITION_OP_INVALID
} ngx_condition_op_e;


typedef struct {
    ngx_str_t           name;
    ngx_condition_id_t  id;
    unsigned            defined:1;
} ngx_condition_name_t;


typedef struct {
    ngx_condition_id_t  condition_id;
    unsigned            negative:1;
} ngx_condition_term_t;


typedef struct {
    ngx_condition_expr_id_t  expr_id;
    ngx_array_t              terms; /* ngx_condition_term_t */
} ngx_condition_when_expr_t;


typedef struct {
    ngx_array_t  names;       /* ngx_condition_name_t */
    ngx_array_t  expressions; /* ngx_condition_when_expr_t */
} ngx_condition_registry_t;


typedef struct {
    sa_family_t  family;
    in_addr_t    in;
#if (NGX_HAVE_INET6)
    u_char       in6[16];
#endif
} ngx_condition_ip_t;


typedef struct {
    sa_family_t  family;
    unsigned     range:1;
    in_addr_t    addr;
    in_addr_t    mask;
    in_addr_t    start;
    in_addr_t    end;
#if (NGX_HAVE_INET6)
    u_char       addr6[16];
    u_char       mask6[16];
#endif
} ngx_condition_ip_item_t;


typedef struct {
    ngx_flag_t               value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_flag_ctx_t;

typedef struct {
    ngx_str_t                value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_str_ctx_t;

typedef struct {
    void                    *value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_ptr_ctx_t;

typedef struct {
    ngx_array_t             *value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_str_array_ctx_t;

typedef struct {
    ngx_array_t             *value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_keyval_ctx_t;

typedef struct {
    ngx_int_t                value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_num_ctx_t;

typedef struct {
    size_t                   value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_size_ctx_t;

typedef struct {
    off_t                    value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_off_ctx_t;

typedef struct {
    ngx_msec_t               value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_msec_ctx_t;

typedef struct {
    time_t                   value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_sec_ctx_t;

typedef struct {
    ngx_bufs_t               value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_bufs_ctx_t;

typedef struct {
    ngx_uint_t               value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_enum_ctx_t;

typedef struct {
    ngx_uint_t               value;
    ngx_condition_expr_id_t  expr_id;
} ngx_conf_condition_bitmask_ctx_t;


typedef ngx_int_t (*ngx_condition_eval_pt)(void *data,
    ngx_condition_expr_id_t expr_id);


ngx_int_t ngx_condition_registry_init(ngx_pool_t *pool,
    ngx_condition_registry_t *registry);
ngx_condition_name_t *ngx_condition_get_or_create_name(ngx_conf_t *cf,
    ngx_condition_registry_t *registry, ngx_str_t *name);
ngx_condition_expr_id_t ngx_condition_get_or_create_when_expr(ngx_conf_t *cf,
    ngx_condition_registry_t *registry, ngx_array_t *terms);
ngx_int_t ngx_condition_validate_names(ngx_conf_t *cf,
    ngx_condition_registry_t *registry);

ngx_int_t ngx_condition_str_eq(ngx_str_t *a, ngx_str_t *b,
    ngx_uint_t ignore_case);
ngx_int_t ngx_condition_str_starts_with(ngx_str_t *value, ngx_str_t *prefix,
    ngx_uint_t ignore_case);
ngx_int_t ngx_condition_str_ends_with(ngx_str_t *value, ngx_str_t *suffix,
    ngx_uint_t ignore_case);
ngx_int_t ngx_condition_str_contains(ngx_str_t *value, ngx_str_t *part,
    ngx_uint_t ignore_case);
ngx_int_t ngx_condition_is_number(ngx_str_t *value);
ngx_int_t ngx_condition_compare_numbers(ngx_str_t *a, ngx_str_t *b,
    ngx_int_t *result);
ngx_int_t ngx_condition_parse_uint_range(ngx_str_t *value,
    ngx_int_t *start, ngx_int_t *end);
ngx_int_t ngx_condition_parse_ip(ngx_str_t *value,
    ngx_condition_ip_t *ip);
ngx_int_t ngx_condition_is_cidr(ngx_str_t *value);
ngx_int_t ngx_condition_parse_ip_item(ngx_str_t *value,
    ngx_condition_ip_item_t *item);
ngx_int_t ngx_condition_ip_item_matches(ngx_condition_ip_t *ip,
    ngx_condition_ip_item_t *item);

ngx_condition_expr_id_t ngx_condition_get_current_expr_id(void);
void ngx_condition_set_current_expr_id(ngx_condition_expr_id_t expr_id);
ngx_condition_expr_id_t ngx_condition_get_associated_expr_id(ngx_conf_t *cf);

void *ngx_condition_find_expr_ctx(ngx_array_t *values,
    ngx_condition_expr_id_t expr_id, size_t element_size,
    size_t expr_id_offset);
void *ngx_conf_get_conditional_ctx(void *data, ngx_array_t *values,
    size_t element_size, size_t expr_id_offset,
    ngx_condition_eval_pt eval);

ngx_int_t ngx_conf_init_conditional_flag_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_flag_t default_value);
ngx_int_t ngx_conf_merge_conditional_flag_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_flag_t default_value);
ngx_int_t ngx_conf_init_conditional_str_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_str_t default_value);
ngx_int_t ngx_conf_merge_conditional_str_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_str_t default_value);
ngx_int_t ngx_conf_init_conditional_ptr_value(ngx_conf_t *cf,
    ngx_array_t **values, void *default_value);
ngx_int_t ngx_conf_merge_conditional_ptr_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, void *default_value);
ngx_int_t ngx_conf_init_conditional_num_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_int_t default_value);
ngx_int_t ngx_conf_merge_conditional_num_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_int_t default_value);
ngx_int_t ngx_conf_init_conditional_size_value(ngx_conf_t *cf,
    ngx_array_t **values, size_t default_value);
ngx_int_t ngx_conf_merge_conditional_size_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, size_t default_value);
ngx_int_t ngx_conf_init_conditional_off_value(ngx_conf_t *cf,
    ngx_array_t **values, off_t default_value);
ngx_int_t ngx_conf_merge_conditional_off_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, off_t default_value);
ngx_int_t ngx_conf_init_conditional_msec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_msec_t default_value);
ngx_int_t ngx_conf_merge_conditional_msec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_msec_t default_value);
ngx_int_t ngx_conf_init_conditional_sec_value(ngx_conf_t *cf,
    ngx_array_t **values, time_t default_value);
ngx_int_t ngx_conf_merge_conditional_sec_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, time_t default_value);
ngx_int_t ngx_conf_init_conditional_bufs_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_num, size_t default_size);
ngx_int_t ngx_conf_merge_conditional_bufs_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_num,
    size_t default_size);
ngx_int_t ngx_conf_init_conditional_enum_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_value);
ngx_int_t ngx_conf_merge_conditional_enum_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_value);
ngx_int_t ngx_conf_init_conditional_bitmask_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_uint_t default_value);
ngx_int_t ngx_conf_merge_conditional_bitmask_value(ngx_conf_t *cf,
    ngx_array_t **values, ngx_array_t *prev, ngx_uint_t default_value);

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


#endif /* _NGX_CONDITION_H_INCLUDED_ */
