#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>

#include "ngx_stream_condition_module.h"


typedef struct ngx_stream_condition_def_s  ngx_stream_condition_def_t;
typedef struct ngx_stream_condition_func_s  ngx_stream_condition_func_t;


typedef struct {
    unsigned                             has_timestamp:1;
    unsigned                             use_local_time:1;
    ngx_stream_complex_value_t           timestamp;
    ngx_condition_range_t                year;
    ngx_condition_range_t                month;
    ngx_condition_range_t                day;
    ngx_condition_range_t                wday;
    ngx_condition_range_t                hour;
    ngx_condition_range_t                min;
    ngx_condition_range_t                sec;
    ngx_int_t                            gmt_offset;
} ngx_stream_condition_time_t;


struct ngx_stream_condition_def_s {
    ngx_stream_condition_func_t         *func;
    unsigned                             ignore_case:1;
    unsigned                             bool_value:1;
    unsigned                             negative:1;
    union {
        ngx_stream_complex_value_t       values[3];

        struct {
            ngx_stream_complex_value_t   value;
            ngx_array_t                  list_values; /* complex values */
        } list;

        struct {
            ngx_stream_complex_value_t   value;
            ngx_condition_ip_ranges_t   *ranges;
        } ip_range;

#if (NGX_PCRE)
        struct {
            ngx_stream_complex_value_t   value;
            ngx_stream_regex_t          *regex;
        } regex_match;
#endif
        ngx_array_t                      terms;       /* ngx_condition_term_t */
        ngx_stream_condition_time_t     *time;
    } u;
};


typedef struct {
    ngx_condition_id_t                   id;
    ngx_condition_func_e                 type;
    ngx_array_t                          definitions; /* definition pointers */
} ngx_stream_condition_scope_entry_t;


typedef struct {
    ngx_array_t                          entries;     /* scope entries */
    ngx_stream_condition_scope_entry_t **effective;
    ngx_uint_t                           effective_nelts;
    unsigned                             finalized:1;
} ngx_stream_condition_srv_conf_t;


typedef struct {
    ngx_condition_registry_t             registry;
} ngx_stream_condition_main_conf_t;


typedef ngx_int_t (*ngx_stream_condition_func_pt)(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);


struct ngx_stream_condition_func_s {
    ngx_str_t                            name;
    ngx_stream_condition_func_pt         handler;
    ngx_condition_func_e                 type;
    ngx_uint_t                           min_args;
    ngx_uint_t                           max_args;
    unsigned                             allow_ignore_case:1;
};


static void *ngx_stream_condition_create_main_conf(ngx_conf_t *cf);
static void *ngx_stream_condition_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_condition_merge_srv_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_stream_condition_postconfiguration(ngx_conf_t *cf);

static char *ngx_stream_condition(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_stream_condition_when(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_stream_condition_scope_entry_t *ngx_stream_condition_find_entry(
    ngx_stream_condition_srv_conf_t *conf, ngx_condition_id_t id);
static ngx_int_t ngx_stream_condition_compile_value(ngx_conf_t *cf,
    ngx_str_t *value, ngx_stream_complex_value_t *complex_value);
static ngx_stream_condition_func_t *ngx_stream_condition_find_func(
    ngx_str_t *name, ngx_uint_t *negative);
static ngx_int_t ngx_stream_condition_parse_definition(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_func_t *func,
    ngx_stream_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_stream_condition_parse_logic(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_stream_condition_parse_time(ngx_conf_t *cf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_stream_condition_parse_ip_range(ngx_conf_t *cf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first);

static ngx_int_t ngx_stream_condition_finalize_scope(ngx_conf_t *cf,
    ngx_stream_condition_srv_conf_t *conf,
    ngx_stream_condition_srv_conf_t *parent);
static ngx_int_t ngx_stream_condition_detect_cycles(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *conf);
static ngx_int_t ngx_stream_condition_visit(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *conf, ngx_condition_id_t id,
    u_char *state);

static ngx_int_t ngx_stream_condition_eval_id(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf, ngx_condition_id_t id,
    ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_logic_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_bool_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_string_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_number_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_time_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_stream_condition_ip_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
#if (NGX_CJSON)
static ngx_int_t ngx_stream_condition_json_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth);
#endif

static ngx_stream_condition_func_t  ngx_stream_condition_funcs[] = {

    { ngx_string("not"),
      ngx_stream_condition_logic_handler,
      NGX_CONDITION_FUNC_NOT,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("and"),
      ngx_stream_condition_logic_handler,
      NGX_CONDITION_FUNC_AND,
      2, NGX_CONDITION_MAX_ARGS,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("or"),
      ngx_stream_condition_logic_handler,
      NGX_CONDITION_FUNC_OR,
      2, NGX_CONDITION_MAX_ARGS,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("bool"),
      ngx_stream_condition_bool_handler,
      NGX_CONDITION_FUNC_BOOL,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("is_empty"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_IS_EMPTY,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("str_eq"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_EQ,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("str_starts_with"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_STARTS_WITH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("str_ends_with"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_ENDS_WITH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("str_contains"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_CONTAINS,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("str_regex_match"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_REGEX_MATCH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("str_in"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_IN,
      2, NGX_CONDITION_MAX_ARGS,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("is_num"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_IS_NUM,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_eq"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_EQ,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_lt"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_LT,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_le"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_LE,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_gt"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_GT,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_ge"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_GE,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_range"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_RANGE,
      2, 3,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("num_in"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_IN,
      2, NGX_CONDITION_MAX_ARGS,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("time_range"),
      ngx_stream_condition_time_handler,
      NGX_CONDITION_FUNC_TIME_RANGE,
      NGX_CONDITION_NO_ARGS, 9,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("is_ip"),
      ngx_stream_condition_ip_handler,
      NGX_CONDITION_FUNC_IS_IP,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("is_cidr"),
      ngx_stream_condition_ip_handler,
      NGX_CONDITION_FUNC_IS_CIDR,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("ip_range"),
      ngx_stream_condition_ip_handler,
      NGX_CONDITION_FUNC_IP_RANGE,
      2, NGX_CONDITION_MAX_ARGS,
      NGX_CONDITION_NO_IGNORE_CASE },

#if (NGX_CJSON)
    { ngx_string("is_json"),
      ngx_stream_condition_json_handler,
      NGX_CONDITION_FUNC_IS_JSON,
      1, 1,
      NGX_CONDITION_NO_IGNORE_CASE },
#endif

    /* Symbol aliases follow the canonical, enum-indexed entries. */
    { ngx_string("="),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_EQ,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("^~"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_STARTS_WITH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("~$"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_ENDS_WITH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("~"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_REGEX_MATCH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("~*"),
      ngx_stream_condition_string_handler,
      NGX_CONDITION_FUNC_STR_REGEX_MATCH,
      2, 2,
      NGX_CONDITION_ALLOW_IGNORE_CASE },

    { ngx_string("=="),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_EQ,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("<"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_LT,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string("<="),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_LE,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string(">"),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_GT,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_string(">="),
      ngx_stream_condition_number_handler,
      NGX_CONDITION_FUNC_NUM_GE,
      2, 2,
      NGX_CONDITION_NO_IGNORE_CASE },

    { ngx_null_string,
      NULL,
      NGX_CONDITION_FUNC_INVALID,
      NGX_CONDITION_NO_ARGS, NGX_CONDITION_NO_ARGS,
      NGX_CONDITION_NO_IGNORE_CASE }
};


static ngx_command_t  ngx_stream_condition_commands[] = {

    { ngx_string("condition"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_2MORE,
      ngx_stream_condition,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("when"),
      NGX_STREAM_MAIN_CONF|NGX_STREAM_SRV_CONF|NGX_CONF_BLOCK|NGX_CONF_1MORE,
      ngx_stream_condition_when,
      NGX_STREAM_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_condition_module_ctx = {
    NULL,                                      /* preconfiguration */
    ngx_stream_condition_postconfiguration,      /* postconfiguration */

    ngx_stream_condition_create_main_conf,       /* create main configuration */
    NULL,                                      /* init main configuration */

    ngx_stream_condition_create_srv_conf,      /* create server configuration */
    ngx_stream_condition_merge_srv_conf        /* merge server configuration */
};


ngx_module_t  ngx_stream_condition_module = {
    NGX_MODULE_V1,
    &ngx_stream_condition_module_ctx,            /* module context */
    ngx_stream_condition_commands,               /* module directives */
    NGX_STREAM_MODULE,                           /* module type */
    NULL,                                      /* init master */
    NULL,                                      /* init module */
    NULL,                                      /* init process */
    NULL,                                      /* init thread */
    NULL,                                      /* exit thread */
    NULL,                                      /* exit process */
    NULL,                                      /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_uint_t
ngx_stream_condition_to_when_cmd_type(ngx_uint_t type)
{
    switch (type) {

    case NGX_STREAM_MAIN_CONF:
        return NGX_STREAM_MAIN_WHEN_CONF;

    case NGX_STREAM_SRV_CONF:
        return NGX_STREAM_SRV_WHEN_CONF;

    default:
        return 0;
    }
}


ngx_uint_t
ngx_stream_condition_from_when_cmd_type(ngx_uint_t type)
{
    switch (type) {

    case NGX_STREAM_MAIN_WHEN_CONF:
        return NGX_STREAM_MAIN_CONF;

    case NGX_STREAM_SRV_WHEN_CONF:
        return NGX_STREAM_SRV_CONF;

    default:
        return type;
    }
}


static ngx_stream_condition_func_t *
ngx_stream_condition_find_func(ngx_str_t *name, ngx_uint_t *negative)
{
    ngx_str_t                      base;
    ngx_stream_condition_func_t   *func;

    base = *name;
    *negative = 0;

    if (name->len > 1 && name->data[0] == '!') {
        if (name->data[1] == '!') {
            return NULL;
        }

        base.len--;
        base.data++;
        *negative = 1;
    }

    for (func = ngx_stream_condition_funcs; func->name.len; func++) {
        if (func->name.len == base.len
            && ngx_strncmp(func->name.data, base.data, base.len) == 0)
        {
            if (*negative && func->type <= NGX_CONDITION_FUNC_LOGIC_LAST) {
                return NULL;
            }

            return func;
        }
    }

    return NULL;
}


static void *
ngx_stream_condition_create_main_conf(ngx_conf_t *cf)
{
    ngx_stream_condition_main_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_condition_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_condition_registry_init(cf->pool, &conf->registry) != NGX_OK) {
        return NULL;
    }

    return conf;
}


static void *
ngx_stream_condition_create_srv_conf(ngx_conf_t *cf)
{
    ngx_stream_condition_srv_conf_t   *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_condition_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->entries, cf->pool, 2,
                       sizeof(ngx_stream_condition_scope_entry_t)) != NGX_OK)
    {
        return NULL;
    }

    return conf;
}


static ngx_stream_condition_scope_entry_t *
ngx_stream_condition_find_entry(ngx_stream_condition_srv_conf_t *conf,
    ngx_condition_id_t id)
{
    ngx_uint_t                            i;
    ngx_stream_condition_scope_entry_t   *entry;

    entry = conf->entries.elts;

    for (i = 0; i < conf->entries.nelts; i++) {
        if (entry[i].id == id) {
            return &entry[i];
        }
    }

    return NULL;
}


static ngx_int_t
ngx_stream_condition_compile_value(ngx_conf_t *cf, ngx_str_t *value,
    ngx_stream_complex_value_t *complex_value)
{
    ngx_stream_compile_complex_value_t   ccv;

    ngx_memzero(&ccv, sizeof(ngx_stream_compile_complex_value_t));
    ngx_memzero(complex_value, sizeof(ngx_stream_complex_value_t));

    ccv.cf = cf;
    ccv.value = value;
    ccv.complex_value = complex_value;

    return ngx_stream_compile_complex_value(&ccv);
}


static ngx_int_t
ngx_stream_condition_parse_logic(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first)
{
    ngx_uint_t   n;

    n = cf->args->nelts - first;
    if (ngx_array_init(&definition->u.terms, cf->pool, n,
                       sizeof(ngx_condition_term_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return ngx_condition_parse_terms(cf, &cmcf->registry, first,
                                     &definition->u.terms);
}


static ngx_int_t
ngx_stream_condition_parse_time(ngx_conf_t *cf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                      part, *value;
    ngx_int_t                      gmt_offset, rc;
    ngx_uint_t                     i, timezone_set;
    ngx_stream_condition_time_t   *time;

    time = ngx_pcalloc(cf->pool, sizeof(ngx_stream_condition_time_t));
    if (time == NULL) {
        return NGX_ERROR;
    }

    time->use_local_time = 1;
    definition->u.time = time;
    value = cf->args->elts;
    timezone_set = 0;
    i = first;

    if (i < cf->args->nelts
        && ngx_strchr(value[i].data, '=') == NULL
        && !(value[i].len >= 3
             && ngx_strncmp(value[i].data, "gmt", 3) == 0))
    {
        if (ngx_stream_condition_compile_value(cf, &value[i],
                                             &time->timestamp) != NGX_OK)
        {
            return NGX_ERROR;
        }

        time->has_timestamp = 1;
        i++;
    }

    for (; i < cf->args->nelts; i++) {
        rc = ngx_condition_parse_timezone(cf, &value[i], &gmt_offset);
        if (rc == NGX_OK) {
            if (timezone_set) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "duplicate time zone \"%V\"", &value[i]);
                return NGX_ERROR;
            }

            timezone_set = 1;
            time->use_local_time = 0;
            time->gmt_offset = gmt_offset;
            continue;
        }

        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

#define ngx_stream_condition_time_field(prefix, member, low, high)           \
        if (value[i].len > sizeof(prefix) - 1                                \
            && ngx_strncmp(value[i].data, prefix, sizeof(prefix) - 1) == 0)  \
        {                                                                    \
            part.data = value[i].data + sizeof(prefix) - 1;                  \
            part.len = value[i].len - (sizeof(prefix) - 1);                  \
            if (ngx_condition_set_time_range(                                \
                    cf, &time->member, &part, low, high, prefix) != NGX_OK)  \
            {                                                                \
                return NGX_ERROR;                                            \
            }                                                                \
            continue;                                                        \
        }

        ngx_stream_condition_time_field("year=", year, 1970, 9999)
        ngx_stream_condition_time_field("month=", month, 1, 12)
        ngx_stream_condition_time_field("day=", day, 1, 31)
        ngx_stream_condition_time_field("wday=", wday, 0, 6)
        ngx_stream_condition_time_field("hour=", hour, 0, 23)
        ngx_stream_condition_time_field("min=", min, 0, 59)
        ngx_stream_condition_time_field("sec=", sec, 0, 59)

#undef ngx_stream_condition_time_field

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid time_range parameter \"%V\"", &value[i]);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_stream_condition_parse_ip_range(ngx_conf_t *cf,
    ngx_stream_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t   *value;

    value = cf->args->elts;

    if (ngx_stream_condition_compile_value(cf, &value[first],
                                         &definition->u.ip_range.value)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return ngx_condition_parse_ip_ranges(cf, first + 1,
                                         &definition->u.ip_range.ranges);
}


static ngx_int_t
ngx_stream_condition_parse_definition(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_func_t *func,
    ngx_stream_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                    *value;
    ngx_uint_t                    argc, i;
    ngx_stream_complex_value_t   *list_value;
#if (NGX_PCRE)
    u_char                        errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t           rc;
#endif

    value = cf->args->elts;
    definition->func = func;

    if (func->name.len == 2
        && ngx_strncmp(func->name.data, "~*", 2) == 0)
    {
        definition->ignore_case = 1;
    }

    if (func->allow_ignore_case && first < cf->args->nelts
        && value[first].len == 2
        && ngx_strncmp(value[first].data, "-i", 2) == 0)
    {
        definition->ignore_case = 1;
        first++;
    }

    argc = cf->args->nelts - first;
    if (argc < func->min_args || argc > func->max_args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number of arguments for condition "
                           "type \"%V\"",
                           &func->name);
        return NGX_ERROR;
    }

    if (func->type >= NGX_CONDITION_FUNC_LOGIC_FIRST
        && func->type <= NGX_CONDITION_FUNC_LOGIC_LAST)
    {
        return ngx_stream_condition_parse_logic(cf, cmcf, definition, first);
    }

    if (func->type == NGX_CONDITION_FUNC_BOOL) {
        if (value[first].len == 4
            && ngx_strncmp(value[first].data, "true", 4) == 0)
        {
            definition->bool_value = 1;
            return NGX_OK;
        }

        if (value[first].len == 5
            && ngx_strncmp(value[first].data, "false", 5) == 0)
        {
            definition->bool_value = 0;
            return NGX_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid bool value \"%V\"; "
                           "expected \"true\" or \"false\"",
                           &value[first]);
        return NGX_ERROR;
    }

    if (func->type == NGX_CONDITION_FUNC_TIME_RANGE) {
        return ngx_stream_condition_parse_time(cf, definition, first);
    }

    if (func->type == NGX_CONDITION_FUNC_IP_RANGE) {
        return ngx_stream_condition_parse_ip_range(cf, definition, first);
    }

    if (func->type == NGX_CONDITION_FUNC_STR_IN
        || func->type == NGX_CONDITION_FUNC_NUM_IN)
    {
        if (ngx_stream_condition_compile_value(cf, &value[first],
                                               &definition->u.list.value)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        if (ngx_array_init(&definition->u.list.list_values, cf->pool,
                           argc - 1, sizeof(ngx_stream_complex_value_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        for (i = 1; i < argc; i++) {
            list_value = ngx_array_push(&definition->u.list.list_values);
            if (list_value == NULL) {
                return NGX_ERROR;
            }

            if (ngx_stream_condition_compile_value(cf, &value[first + i],
                                                   list_value) != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;
    }

    if (func->type == NGX_CONDITION_FUNC_STR_REGEX_MATCH) {
#if (NGX_PCRE)
        if (ngx_stream_condition_compile_value(cf, &value[first],
                                             &definition->u.regex_match.value)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
        rc.pattern = value[first + 1];
        rc.pool = cf->pool;
        rc.err.len = NGX_MAX_CONF_ERRSTR;
        rc.err.data = errstr;
        if (definition->ignore_case) {
            rc.options = NGX_REGEX_CASELESS;
        }

        definition->u.regex_match.regex = ngx_stream_regex_compile(cf, &rc);
        return (definition->u.regex_match.regex != NULL)
                   ? NGX_OK : NGX_ERROR;
#else
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "condition type \"str_regex_match\" requires PCRE");
        return NGX_ERROR;
#endif
    }

    for (i = 0; i < argc; i++) {
        if (ngx_stream_condition_compile_value(cf, &value[first + i],
                                             &definition->u.values[i])
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static char *
ngx_stream_condition(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_condition_srv_conf_t   *cscf = conf;

    ngx_str_t                            *value;
    ngx_condition_id_t                    condition_id;
    ngx_stream_condition_def_t           *definition, **slot;
    ngx_condition_name_t                 *name;
    ngx_stream_condition_main_conf_t     *cmcf;
    ngx_stream_condition_func_t          *func, *modifier;
    ngx_stream_condition_scope_entry_t   *entry;
    ngx_uint_t                            first, modifier_negative, negative;

    (void) cmd;

    value = cf->args->elts;
    cmcf = ngx_stream_conf_get_module_main_conf(cf,
                                                ngx_stream_condition_module);
    if (cmcf == NULL) {
        return NGX_CONF_ERROR;
    }

    name = ngx_condition_get_name(cf, &cmcf->registry, &value[1]);
    if (name == NULL) {
        return NGX_CONF_ERROR;
    }

    condition_id = name->id;

    first = 3;
    func = ngx_stream_condition_find_func(&value[2], &negative);
    if (func == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "unsupported condition type \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    if (func->type == NGX_CONDITION_FUNC_NOT && cf->args->nelts > 3) {
        modifier = ngx_stream_condition_find_func(&value[3],
                                                  &modifier_negative);
        if (modifier != NULL
            && modifier->type > NGX_CONDITION_FUNC_LOGIC_LAST)
        {
            func = modifier;
            negative = !modifier_negative;
            first = 4;
        }
    }

    entry = ngx_stream_condition_find_entry(cscf, condition_id);
    if (entry != NULL && entry->type != func->type) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "condition \"%V\" has conflicting types \"%V\" "
                           "and \"%V\" in the same scope",
                           &value[1],
                           &ngx_stream_condition_funcs[entry->type].name,
                           &func->name);
        return NGX_CONF_ERROR;
    }

    definition = ngx_pcalloc(cf->pool, sizeof(ngx_stream_condition_def_t));
    if (definition == NULL) {
        return NGX_CONF_ERROR;
    }

    definition->negative = negative;

    if (ngx_stream_condition_parse_definition(cf, cmcf, func, definition,
                                              first)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (entry == NULL) {
        entry = ngx_array_push(&cscf->entries);
        if (entry == NULL) {
            return NGX_CONF_ERROR;
        }

        entry->id = condition_id;
        entry->type = func->type;
        if (ngx_array_init(&entry->definitions, cf->pool, 1,
                           sizeof(ngx_stream_condition_def_t *)) != NGX_OK)
        {
            cscf->entries.nelts--;
            return NGX_CONF_ERROR;
        }
    }

    slot = ngx_array_push(&entry->definitions);
    if (slot == NULL) {
        return NGX_CONF_ERROR;
    }

    *slot = definition;
    name = cmcf->registry.names.elts;
    name[condition_id].defined = 1;

    return NGX_CONF_OK;
}


static char *
ngx_stream_condition_when(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_uint_t                          saved_cmd_type, when_type;
    ngx_array_t                         terms;
    ngx_condition_expr_id_t             expr_id, saved_expr_id;
    ngx_stream_condition_main_conf_t   *cmcf;
    char                               *rv;
    ngx_condition_registry_t           *registry;

    (void) cmd;
    (void) conf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf,
                                                ngx_stream_condition_module);
    if (cmcf == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_array_init(&terms, cf->pool, cf->args->nelts - 1,
                       sizeof(ngx_condition_term_t)) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_condition_parse_terms(cf, &cmcf->registry, 1, &terms) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    registry = &cmcf->registry;
    expr_id = ngx_condition_get_when_expr(cf, registry, &terms);
    if (expr_id == NGX_CONDITION_NO_EXPR_ID) {
        return NGX_CONF_ERROR;
    }

    saved_cmd_type = cf->cmd_type;
    when_type = ngx_stream_condition_to_when_cmd_type(saved_cmd_type);
    if (when_type == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "when is not allowed in this context");
        return NGX_CONF_ERROR;
    }

    saved_expr_id = ngx_condition_get_current_expr_id();
    ngx_condition_set_current_expr_id(expr_id);
    cf->cmd_type = when_type;

    rv = ngx_conf_parse(cf, NULL);

    cf->cmd_type = saved_cmd_type;
    ngx_condition_set_current_expr_id(saved_expr_id);

    return rv;
}


static ngx_int_t
ngx_stream_condition_visit(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *conf, ngx_condition_id_t id,
    u_char *state)
{
    ngx_uint_t                            i, j;
    ngx_condition_term_t                 *term;
    ngx_condition_name_t                 *name;
    ngx_stream_condition_def_t          **definition;
    ngx_stream_condition_scope_entry_t   *entry;

    if (id >= conf->effective_nelts || conf->effective[id] == NULL) {
        return NGX_OK;
    }

    if (state[id] == 2) {
        return NGX_OK;
    }

    if (state[id] == 1) {
        name = cmcf->registry.names.elts;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "cycle detected in condition \"%V\"",
                           &name[id].name);
        return NGX_ERROR;
    }

    state[id] = 1;
    entry = conf->effective[id];
    definition = entry->definitions.elts;

    for (i = 0; i < entry->definitions.nelts; i++) {
        if (definition[i]->func->type < NGX_CONDITION_FUNC_LOGIC_FIRST
            || definition[i]->func->type > NGX_CONDITION_FUNC_LOGIC_LAST)
        {
            continue;
        }

        term = definition[i]->u.terms.elts;
        for (j = 0; j < definition[i]->u.terms.nelts; j++) {
            if (ngx_stream_condition_visit(cf, cmcf, conf,
                                           term[j].condition_id, state)
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        }
    }

    state[id] = 2;
    return NGX_OK;
}


static ngx_int_t
ngx_stream_condition_detect_cycles(ngx_conf_t *cf,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *conf)
{
    u_char      *state;
    ngx_uint_t   i;

    if (conf->effective_nelts == 0) {
        return NGX_OK;
    }

    state = ngx_pcalloc(cf->pool, conf->effective_nelts);
    if (state == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < conf->effective_nelts; i++) {
        if (ngx_stream_condition_visit(cf, cmcf, conf, i, state) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_stream_condition_finalize_scope(ngx_conf_t *cf,
    ngx_stream_condition_srv_conf_t *conf,
    ngx_stream_condition_srv_conf_t *parent)
{
    size_t                                size;
    ngx_uint_t                            i;
    ngx_stream_condition_main_conf_t     *cmcf;
    ngx_stream_condition_scope_entry_t   *entry;

    if (conf->finalized) {
        return NGX_OK;
    }

    cmcf = ngx_stream_conf_get_module_main_conf(cf,
                                                ngx_stream_condition_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }

    conf->effective_nelts = cmcf->registry.names.nelts;

    if (conf->effective_nelts != 0) {
        size = conf->effective_nelts
               * sizeof(ngx_stream_condition_scope_entry_t *);
        conf->effective = ngx_pcalloc(cf->pool, size);
        if (conf->effective == NULL) {
            return NGX_ERROR;
        }

        if (parent != NULL) {
            ngx_memcpy(conf->effective, parent->effective,
                       conf->effective_nelts
                       * sizeof(ngx_stream_condition_scope_entry_t *));
        }

        entry = conf->entries.elts;
        for (i = 0; i < conf->entries.nelts; i++) {
            conf->effective[entry[i].id] = &entry[i];
        }
    }

    if (ngx_stream_condition_detect_cycles(cf, cmcf, conf) != NGX_OK) {
        return NGX_ERROR;
    }

    conf->finalized = 1;
    return NGX_OK;
}


static char *
ngx_stream_condition_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_stream_condition_srv_conf_t   *prev = parent;
    ngx_stream_condition_srv_conf_t   *conf = child;

    if (!prev->finalized
        && ngx_stream_condition_finalize_scope(cf, prev, NULL) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_stream_condition_finalize_scope(cf, conf, prev) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_stream_condition_postconfiguration(ngx_conf_t *cf)
{
    ngx_stream_condition_main_conf_t   *cmcf;
    ngx_stream_condition_srv_conf_t    *cscf;

    cmcf = ngx_stream_conf_get_module_main_conf(cf,
                                                ngx_stream_condition_module);
    cscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_condition_module);

    if (cmcf == NULL || cscf == NULL) {
        return NGX_ERROR;
    }

    if (ngx_condition_validate_names(cf, &cmcf->registry) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_stream_condition_finalize_scope(cf, cscf, NULL);
}


static ngx_int_t
ngx_stream_condition_apply_negation(ngx_stream_condition_def_t *definition,
    ngx_int_t result)
{
    return definition->negative ? !result : result;
}


static ngx_int_t
ngx_stream_condition_time_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    time_t                         now;
    ngx_tm_t                       tm;
    ngx_str_t                      value;
    ngx_stream_condition_time_t   *time;

    time = definition->u.time;
    now = ngx_time();

    if (time->has_timestamp) {
        if (ngx_stream_complex_value(s, &time->timestamp, &value) != NGX_OK) {
            return 0;
        }

        now = ngx_atotm(value.data, value.len);
        if (now == (time_t) NGX_ERROR) {
            return 0;
        }
    }

    if (time->use_local_time) {
        ngx_localtime(now, &tm);

    } else {
        ngx_gmtime(now + time->gmt_offset, &tm);
    }

    return ngx_stream_condition_apply_negation(
        definition,
        ngx_condition_range_matches(&time->year, tm.ngx_tm_year)
        && ngx_condition_range_matches(&time->month, tm.ngx_tm_mon)
        && ngx_condition_range_matches(&time->day, tm.ngx_tm_mday)
        && ngx_condition_range_matches(&time->wday, tm.ngx_tm_wday)
        && ngx_condition_range_matches(&time->hour, tm.ngx_tm_hour)
        && ngx_condition_range_matches(&time->min, tm.ngx_tm_min)
        && ngx_condition_range_matches(&time->sec, tm.ngx_tm_sec));
}


static ngx_int_t
ngx_stream_condition_logic_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_int_t               cmp;
    ngx_uint_t              i;
    ngx_condition_term_t   *term;

    if (definition->func->type >= NGX_CONDITION_FUNC_LOGIC_FIRST
        && definition->func->type <= NGX_CONDITION_FUNC_LOGIC_LAST)
    {
        term = definition->u.terms.elts;

        if (definition->func->type == NGX_CONDITION_FUNC_NOT) {
            cmp = ngx_stream_condition_eval_id(s, cmcf, cscf,
                                               term[0].condition_id, depth);
            if (term[0].negative) {
                cmp = !cmp;
            }

            cmp = !cmp;
            ngx_log_debug5(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                           "condition logic, func:%ui ref:%ui negative:%ui "
                           "result:%i depth:%ui",
                           definition->func->type, term[0].condition_id,
                           term[0].negative, cmp, depth);
            return cmp;
        }

        for (i = 0; i < definition->u.terms.nelts; i++) {
            cmp = ngx_stream_condition_eval_id(s, cmcf, cscf,
                                               term[i].condition_id, depth);
            if (term[i].negative) {
                cmp = !cmp;
            }

            if (definition->func->type == NGX_CONDITION_FUNC_AND && !cmp) {
                ngx_log_debug6(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                               "condition logic short circuit, func:%ui "
                               "child:%ui ref:%ui negative:%ui result:%i "
                               "depth:%ui",
                               definition->func->type, i, term[i].condition_id,
                               term[i].negative, cmp, depth);
                return 0;
            }

            if (definition->func->type == NGX_CONDITION_FUNC_OR && cmp) {
                ngx_log_debug6(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                               "condition logic short circuit, func:%ui "
                               "child:%ui ref:%ui negative:%ui result:%i "
                               "depth:%ui",
                               definition->func->type, i, term[i].condition_id,
                               term[i].negative, cmp, depth);
                return 1;
            }
        }

        return definition->func->type == NGX_CONDITION_FUNC_AND;
    }

    return 0;
}


static ngx_int_t
ngx_stream_condition_bool_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    return ngx_stream_condition_apply_negation(definition,
                                               definition->bool_value);
}


static ngx_int_t
ngx_stream_condition_string_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t                      a, b;
    ngx_int_t                      result;
    ngx_uint_t                     i;
    ngx_stream_complex_value_t    *list_value;
    ngx_stream_complex_value_t    *value;

    if (definition->func->type == NGX_CONDITION_FUNC_STR_IN) {
        value = &definition->u.list.value;

    } else if (definition->func->type == NGX_CONDITION_FUNC_STR_REGEX_MATCH) {
#if (NGX_PCRE)
        value = &definition->u.regex_match.value;
#else
        return 0;
#endif

    } else {
        value = &definition->u.values[0];
    }

    if (ngx_stream_complex_value(s, value, &a) != NGX_OK) {
        return 0;
    }

    if (definition->func->type == NGX_CONDITION_FUNC_IS_EMPTY) {
        return ngx_stream_condition_apply_negation(definition, a.len == 0);
    }

    if (definition->func->type == NGX_CONDITION_FUNC_STR_REGEX_MATCH) {
#if (NGX_PCRE)
        result = ngx_stream_regex_exec(s, definition->u.regex_match.regex, &a);
        if (result == NGX_ERROR) {
            return 0;
        }

        return ngx_stream_condition_apply_negation(definition, result >= 0);
#else
        return 0;
#endif
    }

    if (definition->func->type == NGX_CONDITION_FUNC_STR_IN) {
        list_value = definition->u.list.list_values.elts;

        for (i = 0; i < definition->u.list.list_values.nelts; i++) {
            if (ngx_stream_complex_value(s, &list_value[i], &b) != NGX_OK) {
                return 0;
            }

            if (ngx_condition_str_eq(&a, &b, definition->ignore_case)) {
                return ngx_stream_condition_apply_negation(definition, 1);
            }
        }

        return ngx_stream_condition_apply_negation(definition, 0);
    }

    if (ngx_stream_complex_value(s, &definition->u.values[1], &b)
        != NGX_OK)
    {
        return 0;
    }

    switch (definition->func->type) {

    case NGX_CONDITION_FUNC_STR_EQ:
        result = ngx_condition_str_eq(&a, &b, definition->ignore_case);
        break;

    case NGX_CONDITION_FUNC_STR_STARTS_WITH:
        result = ngx_condition_str_starts_with(&a, &b,
                                               definition->ignore_case);
        break;

    case NGX_CONDITION_FUNC_STR_ENDS_WITH:
        result = ngx_condition_str_ends_with(&a, &b,
                                             definition->ignore_case);
        break;

    case NGX_CONDITION_FUNC_STR_CONTAINS:
        result = ngx_condition_str_contains(&a, &b,
                                            definition->ignore_case);
        break;

    default:
        return 0;
    }

    return ngx_stream_condition_apply_negation(definition, result);
}


static ngx_int_t
ngx_stream_condition_number_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t                      a, b, zero;
    ngx_int_t                      result;
    ngx_uint_t                     i;
    ngx_stream_complex_value_t    *list_value;
    ngx_stream_complex_value_t    *value;

    if (definition->func->type == NGX_CONDITION_FUNC_NUM_IN) {
        value = &definition->u.list.value;

    } else {
        value = &definition->u.values[0];
    }

    if (ngx_stream_complex_value(s, value, &a) != NGX_OK) {
        return 0;
    }

    if (definition->func->type == NGX_CONDITION_FUNC_IS_NUM) {
        return ngx_stream_condition_apply_negation(
            definition, ngx_condition_is_number(&a));
    }

    if (definition->func->type == NGX_CONDITION_FUNC_NUM_IN) {
        list_value = definition->u.list.list_values.elts;

        for (i = 0; i < definition->u.list.list_values.nelts; i++) {
            if (ngx_stream_complex_value(s, &list_value[i], &b) != NGX_OK
                || ngx_condition_compare_numbers(&a, &b, &result) != NGX_OK)
            {
                return 0;
            }

            if (result == 0) {
                return ngx_stream_condition_apply_negation(definition, 1);
            }
        }

        return ngx_stream_condition_apply_negation(definition, 0);
    }

    if (ngx_stream_complex_value(s, &definition->u.values[1], &b)
        != NGX_OK)
    {
        return 0;
    }

    if (definition->func->type == NGX_CONDITION_FUNC_NUM_RANGE) {
        if (definition->u.values[2].value.data == NULL) {
            zero.len = 1;
            zero.data = (u_char *) "0";

            if (ngx_condition_compare_numbers(&a, &zero, &result) != NGX_OK
                || result < 0
                || ngx_condition_compare_numbers(&a, &b, &result) != NGX_OK)
            {
                return 0;
            }

            return ngx_stream_condition_apply_negation(definition, result <= 0);
        }

        if (ngx_condition_compare_numbers(&a, &b, &result) != NGX_OK
            || result < 0
            || ngx_stream_complex_value(s, &definition->u.values[2], &b)
               != NGX_OK
            || ngx_condition_compare_numbers(&a, &b, &result) != NGX_OK)
        {
            return 0;
        }

        return ngx_stream_condition_apply_negation(definition, result <= 0);
    }

    if (ngx_condition_compare_numbers(&a, &b, &result) != NGX_OK) {
        return 0;
    }

    switch (definition->func->type) {

    case NGX_CONDITION_FUNC_NUM_EQ:
        result = result == 0;
        break;

    case NGX_CONDITION_FUNC_NUM_LT:
        result = result < 0;
        break;

    case NGX_CONDITION_FUNC_NUM_LE:
        result = result <= 0;
        break;

    case NGX_CONDITION_FUNC_NUM_GT:
        result = result > 0;
        break;

    case NGX_CONDITION_FUNC_NUM_GE:
        result = result >= 0;
        break;

    default:
        return 0;
    }

    return ngx_stream_condition_apply_negation(definition, result);
}


static ngx_int_t
ngx_stream_condition_ip_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t                     value;
    ngx_condition_ip_t            ip;
    ngx_stream_complex_value_t   *complex_value;

    if (definition->func->type == NGX_CONDITION_FUNC_IP_RANGE) {
        complex_value = &definition->u.ip_range.value;

    } else {
        complex_value = &definition->u.values[0];
    }

    if (ngx_stream_complex_value(s, complex_value, &value) != NGX_OK) {
        return 0;
    }

    if (definition->func->type == NGX_CONDITION_FUNC_IS_CIDR) {
        return ngx_stream_condition_apply_negation(
            definition, ngx_condition_is_cidr(&value));
    }

    if (definition->func->type == NGX_CONDITION_FUNC_IS_IP) {
        return ngx_stream_condition_apply_negation(
            definition, ngx_condition_parse_ip(&value, &ip) == NGX_OK);
    }

    if (ngx_condition_parse_ip(&value, &ip) != NGX_OK) {
        return 0;
    }

    return ngx_stream_condition_apply_negation(
        definition,
        ngx_condition_ip_ranges_match(&ip, definition->u.ip_range.ranges));
}


#if (NGX_CJSON)

static ngx_int_t
ngx_stream_condition_json_handler(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf,
    ngx_stream_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t   value;

    if (ngx_stream_complex_value(s, &definition->u.values[0], &value)
        != NGX_OK)
    {
        return 0;
    }

    return ngx_stream_condition_apply_negation(
        definition, ngx_condition_is_json(&value));
}

#endif


static ngx_int_t
ngx_stream_condition_eval_id(ngx_stream_session_t *s,
    ngx_stream_condition_main_conf_t *cmcf,
    ngx_stream_condition_srv_conf_t *cscf, ngx_condition_id_t id,
    ngx_uint_t depth)
{
    ngx_int_t                             result;
    ngx_uint_t                            i;
    ngx_stream_condition_def_t          **definition;
    ngx_stream_condition_scope_entry_t   *entry;

    if (id >= cscf->effective_nelts || cscf->effective[id] == NULL) {
        return 0;
    }

    if (depth > cmcf->registry.names.nelts) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "condition recursion guard triggered for id %ui", id);
        return 0;
    }

    entry = cscf->effective[id];
    definition = entry->definitions.elts;

    for (i = 0; i < entry->definitions.nelts; i++) {
        result = definition[i]->func->handler(s, cmcf, cscf, definition[i],
                                              depth + 1);
        ngx_log_debug5(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                       "condition definition, id:%ui item:%ui type:%ui "
                       "result:%i depth:%ui",
                       id, i, entry->type, result, depth);

        if (result) {
            ngx_log_debug2(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                           "condition definitions OR short circuit, id:%ui "
                           "item:%ui",
                           id, i);
            return 1;
        }
    }

    return 0;
}


ngx_int_t
ngx_stream_condition_get_expr_result(ngx_stream_session_t *s,
    ngx_condition_expr_id_t expr_id)
{
    ngx_uint_t                          i;
    ngx_condition_term_t               *term;
#if (NGX_DEBUG)
    ngx_condition_name_t               *name;
#endif
    ngx_condition_when_expr_t          *expr;
    ngx_stream_condition_main_conf_t   *cmcf;
    ngx_stream_condition_srv_conf_t    *cscf;
    ngx_int_t                           result;
#if (NGX_DEBUG)
    ngx_uint_t                          type;
#endif

    if (expr_id == NGX_CONDITION_NO_EXPR_ID) {
        return NGX_CONDITION_EXPR_HIT;
    }

    cmcf = ngx_stream_get_module_main_conf(s, ngx_stream_condition_module);
    cscf = ngx_stream_get_module_srv_conf(s, ngx_stream_condition_module);

    if (cmcf == NULL || cscf == NULL
        || expr_id >= cmcf->registry.expressions.nelts)
    {
        return NGX_CONDITION_EXPR_MISS;
    }

    expr = cmcf->registry.expressions.elts;
    expr = &expr[expr_id];
    term = expr->terms.elts;
#if (NGX_DEBUG)
    name = cmcf->registry.names.elts;
#endif

    for (i = 0; i < expr->terms.nelts; i++) {
        result = ngx_stream_condition_eval_id(s, cmcf, cscf,
                                              term[i].condition_id, 0);
        if (term[i].negative) {
            result = !result;
        }

#if (NGX_DEBUG)
        type = NGX_CONDITION_FUNC_INVALID;
        if (term[i].condition_id < cscf->effective_nelts
            && cscf->effective[term[i].condition_id] != NULL)
        {
            type = cscf->effective[term[i].condition_id]->type;
        }

        ngx_log_debug7(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                       "condition expression, expr:%ui term:%ui id:%ui "
                       "name:\"%V\" negative:%ui type:%ui result:%i",
                       expr_id, i, term[i].condition_id,
                       &name[term[i].condition_id].name, term[i].negative,
                       type, result);
#endif

        if (!result) {
            ngx_log_debug2(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                           "condition expression AND short circuit, expr:%ui "
                           "term:%ui",
                           expr_id, i);
            return NGX_CONDITION_EXPR_MISS;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "condition expression matched, expr:%ui", expr_id);
    return NGX_CONDITION_EXPR_HIT;
}


char *
ngx_stream_set_conditional_complex_value_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_stream_condition_complex_value_ctx_t),
               offsetof(ngx_stream_condition_complex_value_ctx_t, value),
               offsetof(ngx_stream_condition_complex_value_ctx_t, expr_id),
               ngx_stream_set_complex_value_slot);
}


char *
ngx_stream_set_conditional_complex_value_zero_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_stream_condition_complex_value_ctx_t),
               offsetof(ngx_stream_condition_complex_value_ctx_t, value),
               offsetof(ngx_stream_condition_complex_value_ctx_t, expr_id),
               ngx_stream_set_complex_value_zero_slot);
}


char *
ngx_stream_set_conditional_complex_value_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_stream_condition_complex_value_ctx_t),
               offsetof(ngx_stream_condition_complex_value_ctx_t, value),
               offsetof(ngx_stream_condition_complex_value_ctx_t, expr_id),
               ngx_stream_set_complex_value_size_slot);
}


ngx_int_t
ngx_stream_get_conditional_complex_value(ngx_stream_session_t *s,
    ngx_array_t *values, ngx_str_t *value)
{
    ngx_stream_condition_complex_value_ctx_t   *ctx;

    ctx = ngx_conf_get_conditional_ctx(s, values,
              sizeof(ngx_stream_condition_complex_value_ctx_t),
              offsetof(ngx_stream_condition_complex_value_ctx_t, expr_id),
              ngx_stream_condition_eval_expr);

    if (ctx == NULL || ctx->value == NULL
        || ctx->value == NGX_CONF_UNSET_PTR)
    {
        return NGX_DECLINED;
    }

    return ngx_stream_complex_value(s, ctx->value, value);
}


size_t
ngx_stream_get_conditional_complex_value_size(ngx_stream_session_t *s,
    ngx_array_t *values, size_t default_value)
{
    ngx_stream_condition_complex_value_ctx_t   *ctx;

    ctx = ngx_conf_get_conditional_ctx(s, values,
              sizeof(ngx_stream_condition_complex_value_ctx_t),
              offsetof(ngx_stream_condition_complex_value_ctx_t, expr_id),
              ngx_stream_condition_eval_expr);

    if (ctx == NULL || ctx->value == NGX_CONF_UNSET_PTR) {
        return default_value;
    }

    return ngx_stream_complex_value_size(s, ctx->value, default_value);
}
