#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_CJSON)
#include <cjson/cJSON.h>
#endif

#include "ngx_http_condition_module.h"


#if (NGX_CONDITION)


typedef struct ngx_http_condition_def_s  ngx_http_condition_def_t;


typedef struct {
    unsigned   set:1;
    ngx_int_t  start;
    ngx_int_t  end;
} ngx_http_condition_range_t;


typedef struct {
    unsigned                    has_timestamp:1;
    unsigned                    use_local_time:1;
    ngx_http_complex_value_t    timestamp;
    ngx_http_condition_range_t  year;
    ngx_http_condition_range_t  month;
    ngx_http_condition_range_t  day;
    ngx_http_condition_range_t  wday;
    ngx_http_condition_range_t  hour;
    ngx_http_condition_range_t  min;
    ngx_http_condition_range_t  sec;
    ngx_int_t                   gmt_offset;
} ngx_http_condition_time_t;


struct ngx_http_condition_def_s {
    ngx_condition_op_e             op;
    unsigned                       ignore_case:1;
    ngx_http_complex_value_t       values[3];
    ngx_array_t                   *refs;     /* ngx_condition_id_t */
    ngx_array_t                   *ip_items; /* ngx_condition_ip_item_t */
    ngx_http_condition_time_t     *time;
#if (NGX_PCRE)
    ngx_http_regex_t              *regex;
#endif
};


typedef struct {
    ngx_condition_id_t             id;
    ngx_condition_op_e             type;
    ngx_array_t                     definitions; /* ngx_http_condition_def_t * */
} ngx_http_condition_scope_entry_t;


typedef struct {
    ngx_array_t                     entries; /* ngx_http_condition_scope_entry_t */
    ngx_http_condition_scope_entry_t **effective;
    ngx_uint_t                      effective_nelts;
    unsigned                        finalized:1;
} ngx_http_condition_loc_conf_t;


typedef struct {
    ngx_condition_registry_t        registry;
} ngx_http_condition_main_conf_t;


typedef struct {
    ngx_str_t                       name;
    ngx_condition_op_e              op;
    ngx_uint_t                      min_args;
    ngx_uint_t                      max_args;
    unsigned                        allow_ignore_case:1;
} ngx_http_condition_operator_t;


static void *ngx_http_condition_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_condition_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_condition_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_condition_postconfiguration(ngx_conf_t *cf);

static char *ngx_http_condition_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_condition_when(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_http_condition_operator_t *ngx_http_condition_find_operator(
    ngx_str_t *name);
static ngx_http_condition_scope_entry_t *ngx_http_condition_find_entry(
    ngx_http_condition_loc_conf_t *conf, ngx_condition_id_t id);
static ngx_int_t ngx_http_condition_compile_value(ngx_conf_t *cf,
    ngx_str_t *value, ngx_http_complex_value_t *complex_value);
static ngx_int_t ngx_http_condition_parse_definition(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_operator_t *operator,
    ngx_http_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_http_condition_parse_logic(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_http_condition_parse_time(ngx_conf_t *cf,
    ngx_http_condition_def_t *definition, ngx_uint_t first);
static ngx_int_t ngx_http_condition_parse_ip_range(ngx_conf_t *cf,
    ngx_http_condition_def_t *definition, ngx_uint_t first);

static ngx_int_t ngx_http_condition_finalize_scope(ngx_conf_t *cf,
    ngx_http_condition_loc_conf_t *conf,
    ngx_http_condition_loc_conf_t *parent);
static ngx_int_t ngx_http_condition_detect_cycles(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *conf);
static ngx_int_t ngx_http_condition_visit(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *conf, ngx_condition_id_t id,
    u_char *state);

static ngx_int_t ngx_http_condition_eval_id(ngx_http_request_t *r,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *clcf, ngx_condition_id_t id,
    ngx_uint_t depth);
static ngx_int_t ngx_http_condition_eval_definition(ngx_http_request_t *r,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *clcf,
    ngx_http_condition_def_t *definition, ngx_uint_t depth);
static ngx_int_t ngx_http_condition_eval_time(ngx_http_request_t *r,
    ngx_http_condition_def_t *definition);

static char *ngx_http_condition_set_complex_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *));


static ngx_http_condition_operator_t  ngx_http_condition_operators[] = {
    { ngx_string("not"), NGX_CONDITION_OP_NOT, 1, 1, 0 },
    { ngx_string("and"), NGX_CONDITION_OP_AND, 2, (ngx_uint_t) -1, 0 },
    { ngx_string("or"), NGX_CONDITION_OP_OR, 2, 2, 0 },
    { ngx_string("is_empty"), NGX_CONDITION_OP_IS_EMPTY, 1, 1, 0 },
    { ngx_string("is_not_empty"), NGX_CONDITION_OP_IS_NOT_EMPTY, 1, 1, 0 },
    { ngx_string("str_eq"), NGX_CONDITION_OP_STR_EQ, 2, 2, 1 },
    { ngx_string("str_ne"), NGX_CONDITION_OP_STR_NE, 2, 2, 1 },
    { ngx_string("str_starts_with"), NGX_CONDITION_OP_STR_STARTS_WITH,
      2, 2, 1 },
    { ngx_string("str_ends_with"), NGX_CONDITION_OP_STR_ENDS_WITH,
      2, 2, 1 },
    { ngx_string("str_contains"), NGX_CONDITION_OP_STR_CONTAINS,
      2, 2, 1 },
    { ngx_string("str_regex_match"), NGX_CONDITION_OP_STR_REGEX_MATCH,
      2, 2, 1 },
    { ngx_string("is_num"), NGX_CONDITION_OP_IS_NUM, 1, 1, 0 },
    { ngx_string("num_eq"), NGX_CONDITION_OP_NUM_EQ, 2, 2, 0 },
    { ngx_string("num_ne"), NGX_CONDITION_OP_NUM_NE, 2, 2, 0 },
    { ngx_string("num_lt"), NGX_CONDITION_OP_NUM_LT, 2, 2, 0 },
    { ngx_string("num_le"), NGX_CONDITION_OP_NUM_LE, 2, 2, 0 },
    { ngx_string("num_gt"), NGX_CONDITION_OP_NUM_GT, 2, 2, 0 },
    { ngx_string("num_ge"), NGX_CONDITION_OP_NUM_GE, 2, 2, 0 },
    { ngx_string("num_range"), NGX_CONDITION_OP_NUM_RANGE, 2, 3, 0 },
    { ngx_string("time_range"), NGX_CONDITION_OP_TIME_RANGE, 0, 9, 0 },
    { ngx_string("is_ip"), NGX_CONDITION_OP_IS_IP, 1, 1, 0 },
    { ngx_string("is_cidr"), NGX_CONDITION_OP_IS_CIDR, 1, 1, 0 },
    { ngx_string("ip_range"), NGX_CONDITION_OP_IP_RANGE,
      2, (ngx_uint_t) -1, 0 },
#if (NGX_CJSON)
    { ngx_string("is_json"), NGX_CONDITION_OP_IS_JSON, 1, 1, 0 },
#endif
    { ngx_null_string, NGX_CONDITION_OP_INVALID, 0, 0, 0 }
};


static ngx_command_t  ngx_http_condition_commands[] = {

    { ngx_string("condition"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_2MORE,
      ngx_http_condition_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("when"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
                         |NGX_CONF_BLOCK|NGX_CONF_1MORE,
      ngx_http_condition_when,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_condition_module_ctx = {
    NULL,                                      /* preconfiguration */
    ngx_http_condition_postconfiguration,      /* postconfiguration */

    ngx_http_condition_create_main_conf,       /* create main configuration */
    NULL,                                      /* init main configuration */

    NULL,                                      /* create server configuration */
    NULL,                                      /* merge server configuration */

    ngx_http_condition_create_loc_conf,        /* create location configuration */
    ngx_http_condition_merge_loc_conf          /* merge location configuration */
};


ngx_module_t  ngx_http_condition_module = {
    NGX_MODULE_V1,
    &ngx_http_condition_module_ctx,            /* module context */
    ngx_http_condition_commands,               /* module directives */
    NGX_HTTP_MODULE,                           /* module type */
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
ngx_http_condition_to_when_cmd_type(ngx_uint_t type)
{
    switch (type) {
    case NGX_HTTP_MAIN_CONF:
        return NGX_HTTP_MAIN_WHEN_CONF;
    case NGX_HTTP_SRV_CONF:
        return NGX_HTTP_SRV_WHEN_CONF;
    case NGX_HTTP_LOC_CONF:
        return NGX_HTTP_LOC_WHEN_CONF;
    default:
        return 0;
    }
}


ngx_uint_t
ngx_http_condition_from_when_cmd_type(ngx_uint_t type)
{
    switch (type) {
    case NGX_HTTP_MAIN_WHEN_CONF:
        return NGX_HTTP_MAIN_CONF;
    case NGX_HTTP_SRV_WHEN_CONF:
        return NGX_HTTP_SRV_CONF;
    case NGX_HTTP_LOC_WHEN_CONF:
        return NGX_HTTP_LOC_CONF;
    default:
        return type;
    }
}


static void *
ngx_http_condition_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_condition_main_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_condition_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_condition_registry_init(cf->pool, &conf->registry) != NGX_OK) {
        return NULL;
    }

    return conf;
}


static void *
ngx_http_condition_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_condition_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_condition_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&conf->entries, cf->pool, 2,
                       sizeof(ngx_http_condition_scope_entry_t)) != NGX_OK)
    {
        return NULL;
    }

    return conf;
}


static ngx_http_condition_operator_t *
ngx_http_condition_find_operator(ngx_str_t *name)
{
    ngx_uint_t i;

    for (i = 0; ngx_http_condition_operators[i].name.len; i++) {
        if (ngx_http_condition_operators[i].name.len == name->len
            && ngx_strncmp(ngx_http_condition_operators[i].name.data,
                           name->data, name->len) == 0)
        {
            return &ngx_http_condition_operators[i];
        }
    }

    return NULL;
}


static ngx_http_condition_scope_entry_t *
ngx_http_condition_find_entry(ngx_http_condition_loc_conf_t *conf,
    ngx_condition_id_t id)
{
    ngx_uint_t                         i;
    ngx_http_condition_scope_entry_t  *entry;

    entry = conf->entries.elts;

    for (i = 0; i < conf->entries.nelts; i++) {
        if (entry[i].id == id) {
            return &entry[i];
        }
    }

    return NULL;
}


static ngx_int_t
ngx_http_condition_compile_value(ngx_conf_t *cf, ngx_str_t *value,
    ngx_http_complex_value_t *complex_value)
{
    ngx_http_compile_complex_value_t ccv;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
    ngx_memzero(complex_value, sizeof(ngx_http_complex_value_t));

    ccv.cf = cf;
    ccv.value = value;
    ccv.complex_value = complex_value;

    return ngx_http_compile_complex_value(&ccv);
}


static ngx_int_t
ngx_http_condition_parse_logic(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t             *value;
    ngx_uint_t             i;
    ngx_condition_id_t    *id;
    ngx_condition_name_t  *name;

    definition->refs = ngx_array_create(cf->pool, cf->args->nelts - first,
                                         sizeof(ngx_condition_id_t));
    if (definition->refs == NULL) {
        return NGX_ERROR;
    }

    value = cf->args->elts;

    for (i = first; i < cf->args->nelts; i++) {
        name = ngx_condition_get_or_create_name(cf, &cmcf->registry,
                                                &value[i]);
        if (name == NULL) {
            return NGX_ERROR;
        }

        id = ngx_array_push(definition->refs);
        if (id == NULL) {
            return NGX_ERROR;
        }

        *id = name->id;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_set_time_range(ngx_conf_t *cf,
    ngx_http_condition_range_t *range, ngx_str_t *value,
    ngx_int_t minimum, ngx_int_t maximum, const char *field)
{
    if (range->set
        || ngx_condition_parse_uint_range(value, &range->start,
                                          &range->end) != NGX_OK
        || range->start < minimum || range->end > maximum)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid or duplicate %s range \"%V\"",
                           field, value);
        return NGX_ERROR;
    }

    range->set = 1;
    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_parse_timezone(ngx_conf_t *cf,
    ngx_http_condition_time_t *time, ngx_str_t *value)
{
    ngx_int_t hours, minutes, sign;

    if (value->len == 3
        && ngx_strncmp(value->data, "gmt", 3) == 0)
    {
        time->use_local_time = 0;
        time->gmt_offset = 0;
        return NGX_OK;
    }

    if (value->len != 8
        || ngx_strncmp(value->data, "gmt", 3) != 0
        || (value->data[3] != '+' && value->data[3] != '-'))
    {
        return NGX_DECLINED;
    }

    hours = ngx_atoi(value->data + 4, 2);
    minutes = ngx_atoi(value->data + 6, 2);
    if (hours == NGX_ERROR || minutes == NGX_ERROR
        || hours > 23 || minutes > 59)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid time zone \"%V\"", value);
        return NGX_ERROR;
    }

    sign = (value->data[3] == '-') ? -1 : 1;
    time->use_local_time = 0;
    time->gmt_offset = sign * (hours * 3600 + minutes * 60);

    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_parse_time(ngx_conf_t *cf,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                  part, *value;
    ngx_int_t                  rc;
    ngx_uint_t                 i, timezone_set;
    ngx_http_condition_time_t *time;

    time = ngx_pcalloc(cf->pool, sizeof(ngx_http_condition_time_t));
    if (time == NULL) {
        return NGX_ERROR;
    }

    time->use_local_time = 1;
    definition->time = time;
    value = cf->args->elts;
    timezone_set = 0;
    i = first;

    if (i < cf->args->nelts
        && ngx_strchr(value[i].data, '=') == NULL
        && !(value[i].len >= 3
             && ngx_strncmp(value[i].data, "gmt", 3) == 0))
    {
        if (ngx_http_condition_compile_value(cf, &value[i],
                                             &time->timestamp) != NGX_OK)
        {
            return NGX_ERROR;
        }
        time->has_timestamp = 1;
        i++;
    }

    for (; i < cf->args->nelts; i++) {
        rc = ngx_http_condition_parse_timezone(cf, time, &value[i]);
        if (rc == NGX_OK) {
            if (timezone_set) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "duplicate time zone \"%V\"", &value[i]);
                return NGX_ERROR;
            }
            timezone_set = 1;
            continue;
        }
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

#define ngx_http_condition_time_field(prefix, member, low, high)             \
        if (value[i].len > sizeof(prefix) - 1                                \
            && ngx_strncmp(value[i].data, prefix, sizeof(prefix) - 1) == 0)  \
        {                                                                    \
            part.data = value[i].data + sizeof(prefix) - 1;                  \
            part.len = value[i].len - (sizeof(prefix) - 1);                  \
            if (ngx_http_condition_set_time_range(cf, &time->member, &part,  \
                                                   low, high, prefix)        \
                != NGX_OK)                                                   \
            {                                                                \
                return NGX_ERROR;                                            \
            }                                                                \
            continue;                                                        \
        }

        ngx_http_condition_time_field("year=", year, 1970, 9999)
        ngx_http_condition_time_field("month=", month, 1, 12)
        ngx_http_condition_time_field("day=", day, 1, 31)
        ngx_http_condition_time_field("wday=", wday, 0, 6)
        ngx_http_condition_time_field("hour=", hour, 0, 23)
        ngx_http_condition_time_field("min=", min, 0, 59)
        ngx_http_condition_time_field("sec=", sec, 0, 59)

#undef ngx_http_condition_time_field

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid time_range parameter \"%V\"", &value[i]);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_parse_ip_range(ngx_conf_t *cf,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                *value;
    ngx_uint_t                i;
    ngx_condition_ip_item_t  *item;

    value = cf->args->elts;

    if (ngx_http_condition_compile_value(cf, &value[first],
                                         &definition->values[0]) != NGX_OK)
    {
        return NGX_ERROR;
    }

    definition->ip_items = ngx_array_create(cf->pool,
                               cf->args->nelts - first - 1,
                               sizeof(ngx_condition_ip_item_t));
    if (definition->ip_items == NULL) {
        return NGX_ERROR;
    }

    for (i = first + 1; i < cf->args->nelts; i++) {
        item = ngx_array_push(definition->ip_items);
        if (item == NULL) {
            return NGX_ERROR;
        }

        if (ngx_condition_parse_ip_item(&value[i], item) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid IP item \"%V\"", &value[i]);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_parse_definition(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_operator_t *operator,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                  *value;
    ngx_uint_t                  argc, i;
#if (NGX_PCRE)
    u_char                      errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t         rc;
#endif

    value = cf->args->elts;
    definition->op = operator->op;

    if (operator->allow_ignore_case && first < cf->args->nelts
        && value[first].len == 2
        && ngx_strncmp(value[first].data, "-i", 2) == 0)
    {
        definition->ignore_case = 1;
        first++;
    }

    argc = cf->args->nelts - first;
    if (argc < operator->min_args || argc > operator->max_args) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number of arguments for condition type \"%V\"",
                           &operator->name);
        return NGX_ERROR;
    }

    if (operator->op >= NGX_CONDITION_OP_LOGIC_FIRST
        && operator->op <= NGX_CONDITION_OP_LOGIC_LAST)
    {
        return ngx_http_condition_parse_logic(cf, cmcf, definition, first);
    }

    if (operator->op == NGX_CONDITION_OP_TIME_RANGE) {
        return ngx_http_condition_parse_time(cf, definition, first);
    }

    if (operator->op == NGX_CONDITION_OP_IP_RANGE) {
        return ngx_http_condition_parse_ip_range(cf, definition, first);
    }

    if (operator->op == NGX_CONDITION_OP_STR_REGEX_MATCH) {
#if (NGX_PCRE)
        if (ngx_http_condition_compile_value(cf, &value[first],
                                             &definition->values[0]) != NGX_OK)
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

        definition->regex = ngx_http_regex_compile(cf, &rc);
        return (definition->regex != NULL) ? NGX_OK : NGX_ERROR;
#else
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "condition type \"str_regex_match\" requires PCRE");
        return NGX_ERROR;
#endif
    }

    for (i = 0; i < argc; i++) {
        if (ngx_http_condition_compile_value(cf, &value[first + i],
                                             &definition->values[i]) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static char *
ngx_http_condition_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_condition_loc_conf_t *clcf = conf;

    ngx_str_t                         *value;
    ngx_condition_id_t                condition_id;
    ngx_http_condition_def_t          *definition, **slot;
    ngx_condition_name_t              *name;
    ngx_http_condition_main_conf_t    *cmcf;
    ngx_http_condition_operator_t     *operator;
    ngx_http_condition_scope_entry_t  *entry;

    (void) cmd;

    value = cf->args->elts;
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_condition_module);
    if (cmcf == NULL) {
        return NGX_CONF_ERROR;
    }

    name = ngx_condition_get_or_create_name(cf, &cmcf->registry, &value[1]);
    if (name == NULL) {
        return NGX_CONF_ERROR;
    }
    condition_id = name->id;

    operator = ngx_http_condition_find_operator(&value[2]);
    if (operator == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "unsupported condition type \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    entry = ngx_http_condition_find_entry(clcf, condition_id);
    if (entry != NULL && entry->type != operator->op) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "condition \"%V\" has conflicting types \"%V\" and \"%V\" in the same scope",
                           &value[1],
                           &ngx_http_condition_operators[entry->type].name,
                           &operator->name);
        return NGX_CONF_ERROR;
    }

    definition = ngx_pcalloc(cf->pool, sizeof(ngx_http_condition_def_t));
    if (definition == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_condition_parse_definition(cf, cmcf, operator, definition, 3)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (entry == NULL) {
        entry = ngx_array_push(&clcf->entries);
        if (entry == NULL) {
            return NGX_CONF_ERROR;
        }

        entry->id = condition_id;
        entry->type = operator->op;
        if (ngx_array_init(&entry->definitions, cf->pool, 1,
                           sizeof(ngx_http_condition_def_t *)) != NGX_OK)
        {
            clcf->entries.nelts--;
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
ngx_http_condition_when(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                       name, *value;
    ngx_uint_t                      i, negative, saved_cmd_type, when_type;
    ngx_array_t                     terms;
    ngx_condition_term_t           *term;
    ngx_condition_name_t           *entry;
    ngx_condition_expr_id_t         expr_id, saved_expr_id;
    ngx_http_condition_main_conf_t *cmcf;
    char                            *rv;

    (void) cmd;
    (void) conf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_condition_module);
    if (cmcf == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_array_init(&terms, cf->pool, cf->args->nelts - 1,
                       sizeof(ngx_condition_term_t)) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        name = value[i];
        negative = (name.len != 0 && name.data[0] == '!');
        if (negative) {
            name.data++;
            name.len--;
        }

        entry = ngx_condition_get_or_create_name(cf, &cmcf->registry, &name);
        if (entry == NULL) {
            return NGX_CONF_ERROR;
        }

        term = ngx_array_push(&terms);
        if (term == NULL) {
            return NGX_CONF_ERROR;
        }
        term->condition_id = entry->id;
        term->negative = negative;
    }

    expr_id = ngx_condition_get_or_create_when_expr(cf, &cmcf->registry,
                                                     &terms);
    if (expr_id == NGX_CONDITION_NO_EXPR_ID) {
        return NGX_CONF_ERROR;
    }

    saved_cmd_type = cf->cmd_type;
    when_type = ngx_http_condition_to_when_cmd_type(saved_cmd_type);
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
ngx_http_condition_visit(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *conf, ngx_condition_id_t id,
    u_char *state)
{
    ngx_uint_t                         i, j;
    ngx_condition_id_t                *ref;
    ngx_condition_name_t              *name;
    ngx_http_condition_def_t         **definition;
    ngx_http_condition_scope_entry_t  *entry;

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
        if (definition[i]->op < NGX_CONDITION_OP_LOGIC_FIRST
            || definition[i]->op > NGX_CONDITION_OP_LOGIC_LAST)
        {
            continue;
        }

        ref = definition[i]->refs->elts;
        for (j = 0; j < definition[i]->refs->nelts; j++) {
            if (ngx_http_condition_visit(cf, cmcf, conf, ref[j], state)
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
ngx_http_condition_detect_cycles(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *conf)
{
    u_char     *state;
    ngx_uint_t  i;

    if (conf->effective_nelts == 0) {
        return NGX_OK;
    }

    state = ngx_pcalloc(cf->pool, conf->effective_nelts);
    if (state == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < conf->effective_nelts; i++) {
        if (ngx_http_condition_visit(cf, cmcf, conf, i, state) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_condition_finalize_scope(ngx_conf_t *cf,
    ngx_http_condition_loc_conf_t *conf,
    ngx_http_condition_loc_conf_t *parent)
{
    ngx_uint_t                         i;
    ngx_http_condition_main_conf_t    *cmcf;
    ngx_http_condition_scope_entry_t  *entry;

    if (conf->finalized) {
        return NGX_OK;
    }

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_condition_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }

    conf->effective_nelts = cmcf->registry.names.nelts;

    if (conf->effective_nelts != 0) {
        conf->effective = ngx_pcalloc(cf->pool,
                              conf->effective_nelts
                              * sizeof(ngx_http_condition_scope_entry_t *));
        if (conf->effective == NULL) {
            return NGX_ERROR;
        }

        if (parent != NULL) {
            ngx_memcpy(conf->effective, parent->effective,
                       conf->effective_nelts
                       * sizeof(ngx_http_condition_scope_entry_t *));
        }

        entry = conf->entries.elts;
        for (i = 0; i < conf->entries.nelts; i++) {
            conf->effective[entry[i].id] = &entry[i];
        }
    }

    if (ngx_http_condition_detect_cycles(cf, cmcf, conf) != NGX_OK) {
        return NGX_ERROR;
    }

    conf->finalized = 1;
    return NGX_OK;
}


static char *
ngx_http_condition_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_condition_loc_conf_t *prev = parent;
    ngx_http_condition_loc_conf_t *conf = child;

    if (!prev->finalized
        && ngx_http_condition_finalize_scope(cf, prev, NULL) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_condition_finalize_scope(cf, conf, prev) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_condition_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_condition_main_conf_t *cmcf;
    ngx_http_condition_loc_conf_t  *clcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_condition_module);
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_condition_module);

    if (cmcf == NULL || clcf == NULL) {
        return NGX_ERROR;
    }

    if (ngx_condition_validate_names(cf, &cmcf->registry) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_http_condition_finalize_scope(cf, clcf, NULL);
}


static ngx_int_t
ngx_http_condition_range_matches(ngx_http_condition_range_t *range,
    ngx_int_t value)
{
    return !range->set || (value >= range->start && value <= range->end);
}


static ngx_int_t
ngx_http_condition_eval_time(ngx_http_request_t *r,
    ngx_http_condition_def_t *definition)
{
    time_t                      now;
    ngx_tm_t                    tm;
    ngx_str_t                   value;
    ngx_http_condition_time_t  *time;

    time = definition->time;
    now = ngx_time();

    if (time->has_timestamp) {
        if (ngx_http_complex_value(r, &time->timestamp, &value) != NGX_OK) {
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

    return ngx_http_condition_range_matches(&time->year, tm.ngx_tm_year)
           && ngx_http_condition_range_matches(&time->month, tm.ngx_tm_mon)
           && ngx_http_condition_range_matches(&time->day, tm.ngx_tm_mday)
           && ngx_http_condition_range_matches(&time->wday, tm.ngx_tm_wday)
           && ngx_http_condition_range_matches(&time->hour, tm.ngx_tm_hour)
           && ngx_http_condition_range_matches(&time->min, tm.ngx_tm_min)
           && ngx_http_condition_range_matches(&time->sec, tm.ngx_tm_sec);
}


#if (NGX_CJSON)
static ngx_int_t
ngx_http_condition_is_json(ngx_str_t *value)
{
    const char *end;
    cJSON      *json;

    if (value->len == 0) {
        return 0;
    }

    end = NULL;
    json = cJSON_ParseWithLengthOpts((const char *) value->data, value->len,
                                     &end, 0);
    if (json == NULL || end == NULL) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return 0;
    }

    while (end < (const char *) value->data + value->len
           && (*end == ' ' || *end == '\t' || *end == CR || *end == LF))
    {
        end++;
    }

    cJSON_Delete(json);
    return end == (const char *) value->data + value->len;
}
#endif


static ngx_int_t
ngx_http_condition_eval_definition(ngx_http_request_t *r,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *clcf,
    ngx_http_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t                 a, b, zero;
    ngx_int_t                 cmp;
    ngx_uint_t                i;
    ngx_condition_id_t       *ref;
    ngx_condition_ip_t        ip;
    ngx_condition_ip_item_t  *item;

    if (definition->op >= NGX_CONDITION_OP_LOGIC_FIRST
        && definition->op <= NGX_CONDITION_OP_LOGIC_LAST)
    {
        ref = definition->refs->elts;

        if (definition->op == NGX_CONDITION_OP_NOT) {
            cmp = !ngx_http_condition_eval_id(r, cmcf, clcf, ref[0], depth);
            ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition logic, op:%ui ref:%ui result:%i depth:%ui",
                           definition->op, ref[0], cmp, depth);
            return cmp;
        }

        for (i = 0; i < definition->refs->nelts; i++) {
            cmp = ngx_http_condition_eval_id(r, cmcf, clcf, ref[i], depth);

            if (definition->op == NGX_CONDITION_OP_AND && !cmp) {
                ngx_log_debug5(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "condition logic short circuit, op:%ui child:%ui ref:%ui result:%i depth:%ui",
                               definition->op, i, ref[i], cmp, depth);
                return 0;
            }

            if (definition->op == NGX_CONDITION_OP_OR && cmp) {
                ngx_log_debug5(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "condition logic short circuit, op:%ui child:%ui ref:%ui result:%i depth:%ui",
                               definition->op, i, ref[i], cmp, depth);
                return 1;
            }
        }

        return definition->op == NGX_CONDITION_OP_AND;
    }

    if (definition->op == NGX_CONDITION_OP_TIME_RANGE) {
        return ngx_http_condition_eval_time(r, definition);
    }

    if (ngx_http_complex_value(r, &definition->values[0], &a) != NGX_OK) {
        return 0;
    }

    switch (definition->op) {
    case NGX_CONDITION_OP_IS_EMPTY:
        return a.len == 0;
    case NGX_CONDITION_OP_IS_NOT_EMPTY:
        return a.len != 0;
    case NGX_CONDITION_OP_IS_NUM:
        return ngx_condition_is_number(&a);
    case NGX_CONDITION_OP_IS_IP:
        return ngx_condition_parse_ip(&a, &ip) == NGX_OK;
    case NGX_CONDITION_OP_IS_CIDR:
        return ngx_condition_is_cidr(&a);
#if (NGX_CJSON)
    case NGX_CONDITION_OP_IS_JSON:
        return ngx_http_condition_is_json(&a);
#endif
    case NGX_CONDITION_OP_STR_REGEX_MATCH:
#if (NGX_PCRE)
        return ngx_http_regex_exec(r, definition->regex, &a) >= 0;
#else
        return 0;
#endif
    case NGX_CONDITION_OP_IP_RANGE:
        if (ngx_condition_parse_ip(&a, &ip) != NGX_OK) {
            return 0;
        }
        item = definition->ip_items->elts;
        for (i = 0; i < definition->ip_items->nelts; i++) {
            if (ngx_condition_ip_item_matches(&ip, &item[i])) {
                return 1;
            }
        }
        return 0;
    default:
        break;
    }

    if (ngx_http_complex_value(r, &definition->values[1], &b) != NGX_OK) {
        return 0;
    }

    switch (definition->op) {
    case NGX_CONDITION_OP_STR_EQ:
        return ngx_condition_str_eq(&a, &b, definition->ignore_case);
    case NGX_CONDITION_OP_STR_NE:
        return !ngx_condition_str_eq(&a, &b, definition->ignore_case);
    case NGX_CONDITION_OP_STR_STARTS_WITH:
        return ngx_condition_str_starts_with(&a, &b,
                                              definition->ignore_case);
    case NGX_CONDITION_OP_STR_ENDS_WITH:
        return ngx_condition_str_ends_with(&a, &b,
                                            definition->ignore_case);
    case NGX_CONDITION_OP_STR_CONTAINS:
        return ngx_condition_str_contains(&a, &b,
                                           definition->ignore_case);
    default:
        break;
    }

    if (definition->op == NGX_CONDITION_OP_NUM_RANGE) {
        if (definition->values[2].value.data == NULL) {
            zero.len = 1;
            zero.data = (u_char *) "0";

            if (ngx_condition_compare_numbers(&a, &zero, &cmp) != NGX_OK
                || cmp < 0
                || ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK)
            {
                return 0;
            }

            return cmp <= 0;
        }

        if (ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK
            || cmp < 0
            || ngx_http_complex_value(r, &definition->values[2], &b) != NGX_OK
            || ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK)
        {
            return 0;
        }

        return cmp <= 0;
    }

    if (ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK) {
        return 0;
    }

    switch (definition->op) {
    case NGX_CONDITION_OP_NUM_EQ:
        return cmp == 0;
    case NGX_CONDITION_OP_NUM_NE:
        return cmp != 0;
    case NGX_CONDITION_OP_NUM_LT:
        return cmp < 0;
    case NGX_CONDITION_OP_NUM_LE:
        return cmp <= 0;
    case NGX_CONDITION_OP_NUM_GT:
        return cmp > 0;
    case NGX_CONDITION_OP_NUM_GE:
        return cmp >= 0;
    default:
        return 0;
    }
}


static ngx_int_t
ngx_http_condition_eval_id(ngx_http_request_t *r,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *clcf, ngx_condition_id_t id,
    ngx_uint_t depth)
{
    ngx_int_t                          result;
    ngx_uint_t                         i;
    ngx_http_condition_def_t         **definition;
    ngx_http_condition_scope_entry_t  *entry;

    if (id >= clcf->effective_nelts || clcf->effective[id] == NULL) {
        return 0;
    }

    if (depth > cmcf->registry.names.nelts) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "condition recursion guard triggered for id %ui", id);
        return 0;
    }

    entry = clcf->effective[id];
    definition = entry->definitions.elts;

    for (i = 0; i < entry->definitions.nelts; i++) {
        result = ngx_http_condition_eval_definition(r, cmcf, clcf,
                                                     definition[i], depth + 1);
        ngx_log_debug5(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "condition definition, id:%ui item:%ui type:%ui result:%i depth:%ui",
                       id, i, entry->type, result, depth);

        if (result) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition definitions OR short circuit, id:%ui item:%ui",
                           id, i);
            return 1;
        }
    }

    return 0;
}


ngx_int_t
ngx_http_condition_get_expr_result(ngx_http_request_t *r,
    ngx_condition_expr_id_t expr_id)
{
    ngx_uint_t                       i;
    ngx_condition_term_t            *term;
#if (NGX_DEBUG)
    ngx_condition_name_t            *name;
#endif
    ngx_condition_when_expr_t       *expr;
    ngx_http_condition_main_conf_t  *cmcf;
    ngx_http_condition_loc_conf_t   *clcf;
    ngx_int_t                        result;
#if (NGX_DEBUG)
    ngx_uint_t                       type;
#endif

    if (expr_id == NGX_CONDITION_NO_EXPR_ID) {
        return NGX_CONDITION_EXPR_HIT;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_condition_module);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_condition_module);

    if (cmcf == NULL || clcf == NULL
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
        result = ngx_http_condition_eval_id(r, cmcf, clcf,
                                            term[i].condition_id, 0);
        if (term[i].negative) {
            result = !result;
        }

#if (NGX_DEBUG)
        type = NGX_CONDITION_OP_INVALID;
        if (term[i].condition_id < clcf->effective_nelts
            && clcf->effective[term[i].condition_id] != NULL)
        {
            type = clcf->effective[term[i].condition_id]->type;
        }

        ngx_log_debug7(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "condition expression, expr:%ui term:%ui id:%ui name:\"%V\" negative:%ui type:%ui result:%i",
                       expr_id, i, term[i].condition_id,
                       &name[term[i].condition_id].name, term[i].negative,
                       type, result);
#endif

        if (!result) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition expression AND short circuit, expr:%ui term:%ui",
                           expr_id, i);
            return NGX_CONDITION_EXPR_MISS;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "condition expression matched, expr:%ui", expr_id);
    return NGX_CONDITION_EXPR_HIT;
}


static char *
ngx_http_condition_set_complex_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, char *(*setter)(ngx_conf_t *, ngx_command_t *, void *))
{
    char                                      *rv;
    ngx_uint_t                                 created;
    ngx_array_t                              **values;
    ngx_command_t                              local_cmd;
    ngx_http_condition_complex_value_ctx_t    *ctx;

    values = (ngx_array_t **) ((u_char *) conf + cmd->offset);

    if (*values == NULL || *values == NGX_CONF_UNSET_PTR) {
        *values = ngx_array_create(cf->pool, 2,
                     sizeof(ngx_http_condition_complex_value_ctx_t));
        if (*values == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    ctx = ngx_condition_find_expr_ctx(*values,
              ngx_condition_get_associated_expr_id(cf),
              sizeof(ngx_http_condition_complex_value_ctx_t),
              offsetof(ngx_http_condition_complex_value_ctx_t, expr_id));

    created = 0;

    if (ctx == NULL) {
        ctx = ngx_array_push(*values);
        if (ctx == NULL) {
            return NGX_CONF_ERROR;
        }

        ctx->value = NGX_CONF_UNSET_PTR;
        ctx->expr_id = ngx_condition_get_associated_expr_id(cf);
        created = 1;
    }

    local_cmd = *cmd;
    local_cmd.offset = offsetof(ngx_http_condition_complex_value_ctx_t,
                                value);

    rv = setter(cf, &local_cmd, ctx);

    if (rv != NGX_CONF_OK && created) {
        (*values)->nelts--;
    }

    return rv;
}


char *
ngx_http_set_conditional_complex_value_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_http_condition_set_complex_slot(cf, cmd, conf,
               ngx_http_set_complex_value_slot);
}


char *
ngx_http_set_conditional_complex_value_zero_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_http_condition_set_complex_slot(cf, cmd, conf,
               ngx_http_set_complex_value_zero_slot);
}


char *
ngx_http_set_conditional_complex_value_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_http_condition_set_complex_slot(cf, cmd, conf,
               ngx_http_set_complex_value_size_slot);
}


#if (NGX_RESTY_EXT)

char *
ngx_http_set_conditional_complex_value_msec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_http_condition_set_complex_slot(cf, cmd, conf,
               ngx_http_set_complex_value_msec_slot);
}


char *
ngx_http_set_conditional_complex_value_sec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_http_condition_set_complex_slot(cf, cmd, conf,
               ngx_http_set_complex_value_sec_slot);
}

#endif


ngx_int_t
ngx_http_get_conditional_complex_value(ngx_http_request_t *r,
    ngx_array_t *values, ngx_str_t *value)
{
    ngx_http_condition_complex_value_ctx_t  *ctx;

    ctx = ngx_conf_get_conditional_ctx(r, values,
              sizeof(ngx_http_condition_complex_value_ctx_t),
              offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
              ngx_http_condition_eval_expr);

    if (ctx == NULL || ctx->value == NULL
        || ctx->value == NGX_CONF_UNSET_PTR)
    {
        return NGX_DECLINED;
    }

    return ngx_http_complex_value(r, ctx->value, value);
}


size_t
ngx_http_get_conditional_complex_value_size(ngx_http_request_t *r,
    ngx_array_t *values, size_t default_value)
{
    ngx_http_condition_complex_value_ctx_t  *ctx;

    ctx = ngx_conf_get_conditional_ctx(r, values,
              sizeof(ngx_http_condition_complex_value_ctx_t),
              offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
              ngx_http_condition_eval_expr);

    if (ctx == NULL || ctx->value == NGX_CONF_UNSET_PTR) {
        return default_value;
    }

    return ngx_http_complex_value_size(r, ctx->value, default_value);
}


#if (NGX_RESTY_EXT)

ngx_msec_t
ngx_http_get_conditional_complex_value_msec(ngx_http_request_t *r,
    ngx_array_t *values, ngx_msec_t default_value)
{
    ngx_http_condition_complex_value_ctx_t  *ctx;

    ctx = ngx_conf_get_conditional_ctx(r, values,
              sizeof(ngx_http_condition_complex_value_ctx_t),
              offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
              ngx_http_condition_eval_expr);

    if (ctx == NULL || ctx->value == NGX_CONF_UNSET_PTR) {
        return default_value;
    }

    return ngx_http_complex_value_msec(r, ctx->value, default_value);
}


time_t
ngx_http_get_conditional_complex_value_sec(ngx_http_request_t *r,
    ngx_array_t *values, time_t default_value)
{
    ngx_http_condition_complex_value_ctx_t  *ctx;

    ctx = ngx_conf_get_conditional_ctx(r, values,
              sizeof(ngx_http_condition_complex_value_ctx_t),
              offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
              ngx_http_condition_eval_expr);

    if (ctx == NULL || ctx->value == NGX_CONF_UNSET_PTR) {
        return default_value;
    }

    return ngx_http_complex_value_sec(r, ctx->value, default_value);
}

#endif


#endif /* NGX_CONDITION */
