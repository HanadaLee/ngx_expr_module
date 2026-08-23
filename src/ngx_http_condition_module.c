#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_condition_module.h"


typedef struct ngx_http_condition_def_s  ngx_http_condition_def_t;


typedef struct {
    unsigned                           has_timestamp:1;
    unsigned                           use_local_time:1;
    ngx_http_complex_value_t           timestamp;
    ngx_condition_range_t              year;
    ngx_condition_range_t              month;
    ngx_condition_range_t              day;
    ngx_condition_range_t              wday;
    ngx_condition_range_t              hour;
    ngx_condition_range_t              min;
    ngx_condition_range_t              sec;
    ngx_int_t                          gmt_offset;
} ngx_http_condition_time_t;


struct ngx_http_condition_def_s {
    ngx_condition_func_e               func;
    unsigned                           ignore_case:1;
    unsigned                           bool_value:1;
    unsigned                           negative:1;
    ngx_http_complex_value_t           values[3];
    ngx_array_t                       *list_values; /* complex values */
    ngx_array_t                       *terms;       /* ngx_condition_term_t */
    ngx_array_t                       *ip_items;    /* ngx_condition_ip_item_t */
    ngx_http_condition_time_t         *time;
#if (NGX_PCRE)
    ngx_http_regex_t                  *regex;
#endif
};


typedef struct {
    ngx_condition_id_t                 id;
    ngx_condition_func_e               type;
    ngx_array_t                        definitions; /* definition pointers */
} ngx_http_condition_scope_entry_t;


typedef struct {
    ngx_array_t                        entries;     /* scope entries */
    ngx_http_condition_scope_entry_t **effective;
    ngx_uint_t                         effective_nelts;
    unsigned                           finalized:1;
} ngx_http_condition_loc_conf_t;


typedef struct {
    ngx_condition_registry_t           registry;
} ngx_http_condition_main_conf_t;


static void *ngx_http_condition_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_condition_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_condition_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_condition_postconfiguration(ngx_conf_t *cf);

static char *ngx_http_condition_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_condition_when(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_http_condition_scope_entry_t *ngx_http_condition_find_entry(
    ngx_http_condition_loc_conf_t *conf, ngx_condition_id_t id);
static ngx_int_t ngx_http_condition_compile_value(ngx_conf_t *cf,
    ngx_str_t *value, ngx_http_complex_value_t *complex_value);
static ngx_int_t ngx_http_condition_parse_definition(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    const ngx_condition_func_t *func,
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

    ngx_http_condition_create_loc_conf,        /* create location conf */
    ngx_http_condition_merge_loc_conf          /* merge location conf */
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
    ngx_uint_t  n;

    n = cf->args->nelts - first;
    definition->terms = ngx_array_create(cf->pool, n,
                                         sizeof(ngx_condition_term_t));
    if (definition->terms == NULL) {
        return NGX_ERROR;
    }

    return ngx_condition_parse_terms(cf, &cmcf->registry, first,
                                     definition->terms);
}


static ngx_int_t
ngx_http_condition_parse_time(ngx_conf_t *cf,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                  part, *value;
    ngx_int_t                  gmt_offset, rc;
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

#define ngx_http_condition_time_field(prefix, member, low, high)             \
        if (value[i].len > sizeof(prefix) - 1                                \
            && ngx_strncmp(value[i].data, prefix, sizeof(prefix) - 1) == 0)  \
        {                                                                    \
            part.data = value[i].data + sizeof(prefix) - 1;                  \
            part.len = value[i].len - (sizeof(prefix) - 1);                  \
            if (ngx_condition_set_time_range(cf, &time->member, &part,       \
                                              low, high, prefix)             \
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
    ngx_str_t  *value;

    value = cf->args->elts;

    if (ngx_http_condition_compile_value(cf, &value[first],
                                         &definition->values[0]) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return ngx_condition_parse_ip_items(cf, first + 1,
                                        &definition->ip_items);
}


static ngx_int_t
ngx_http_condition_parse_definition(ngx_conf_t *cf,
    ngx_http_condition_main_conf_t *cmcf,
    const ngx_condition_func_t *func,
    ngx_http_condition_def_t *definition, ngx_uint_t first)
{
    ngx_str_t                  *value;
    ngx_uint_t                  argc, i;
    ngx_http_complex_value_t   *list_value;
#if (NGX_PCRE)
    u_char                      errstr[NGX_MAX_CONF_ERRSTR];
    ngx_regex_compile_t         rc;
#endif

    value = cf->args->elts;
    definition->func = func->type;

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
        return ngx_http_condition_parse_logic(cf, cmcf, definition, first);
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
        return ngx_http_condition_parse_time(cf, definition, first);
    }

    if (func->type == NGX_CONDITION_FUNC_IP_RANGE) {
        return ngx_http_condition_parse_ip_range(cf, definition, first);
    }

    if (func->type == NGX_CONDITION_FUNC_STR_IN
        || func->type == NGX_CONDITION_FUNC_NUM_IN)
    {
        if (ngx_http_condition_compile_value(cf, &value[first],
                                             &definition->values[0]) != NGX_OK)
        {
            return NGX_ERROR;
        }

        definition->list_values = ngx_array_create(
            cf->pool, argc - 1, sizeof(ngx_http_complex_value_t));
        if (definition->list_values == NULL) {
            return NGX_ERROR;
        }

        for (i = 1; i < argc; i++) {
            list_value = ngx_array_push(definition->list_values);
            if (list_value == NULL) {
                return NGX_ERROR;
            }

            if (ngx_http_condition_compile_value(cf, &value[first + i],
                                                 list_value) != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;
    }

    if (func->type == NGX_CONDITION_FUNC_STR_REGEX_MATCH) {
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

    ngx_str_t                        *value;
    ngx_condition_id_t                condition_id;
    ngx_http_condition_def_t         *definition, **slot;
    ngx_condition_name_t             *name;
    ngx_http_condition_main_conf_t   *cmcf;
    const ngx_condition_func_t      *func, *modifier;
    ngx_http_condition_scope_entry_t *entry;
    ngx_uint_t                        first, modifier_negative, negative;

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

    first = 3;
    func = ngx_condition_find_func(&value[2], &negative);
    if (func == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "unsupported condition type \"%V\"", &value[2]);
        return NGX_CONF_ERROR;
    }

    if (func->type == NGX_CONDITION_FUNC_NOT && cf->args->nelts > 3) {
        modifier = ngx_condition_find_func(&value[3], &modifier_negative);
        if (modifier != NULL
            && modifier->type > NGX_CONDITION_FUNC_LOGIC_LAST)
        {
            func = modifier;
            negative = !modifier_negative;
            first = 4;
        }
    }

    entry = ngx_http_condition_find_entry(clcf, condition_id);
    if (entry != NULL && entry->type != func->type) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "condition \"%V\" has conflicting types \"%V\" "
                           "and \"%V\" in the same scope",
                           &value[1],
                           &ngx_condition_funcs[entry->type].name,
                           &func->name);
        return NGX_CONF_ERROR;
    }

    definition = ngx_pcalloc(cf->pool, sizeof(ngx_http_condition_def_t));
    if (definition == NULL) {
        return NGX_CONF_ERROR;
    }

    definition->negative = negative;

    if (ngx_http_condition_parse_definition(cf, cmcf, func, definition,
                                            first)
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
        entry->type = func->type;
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
    ngx_uint_t                      saved_cmd_type, when_type;
    ngx_array_t                     terms;
    ngx_condition_expr_id_t         expr_id, saved_expr_id;
    ngx_http_condition_main_conf_t *cmcf;
    char                           *rv;
    ngx_condition_registry_t       *registry;

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

    if (ngx_condition_parse_terms(cf, &cmcf->registry, 1, &terms) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    registry = &cmcf->registry;
    expr_id = ngx_condition_get_when_expr(cf, registry, &terms);
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
    ngx_condition_term_t              *term;
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
        if (definition[i]->func < NGX_CONDITION_FUNC_LOGIC_FIRST
            || definition[i]->func > NGX_CONDITION_FUNC_LOGIC_LAST)
        {
            continue;
        }

        term = definition[i]->terms->elts;
        for (j = 0; j < definition[i]->terms->nelts; j++) {
            if (ngx_http_condition_visit(cf, cmcf, conf,
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
    size_t                             size;
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
        size = conf->effective_nelts
               * sizeof(ngx_http_condition_scope_entry_t *);
        conf->effective = ngx_pcalloc(cf->pool, size);
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
ngx_http_condition_apply_negation(ngx_http_condition_def_t *definition,
    ngx_int_t result)
{
    return definition->negative ? !result : result;
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

    return ngx_http_condition_apply_negation(
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
ngx_http_condition_eval_definition(ngx_http_request_t *r,
    ngx_http_condition_main_conf_t *cmcf,
    ngx_http_condition_loc_conf_t *clcf,
    ngx_http_condition_def_t *definition, ngx_uint_t depth)
{
    ngx_str_t                  a, b, zero;
    ngx_int_t                  cmp;
    ngx_uint_t                 i;
    ngx_condition_term_t      *term;
    ngx_condition_ip_t         ip;
    ngx_condition_ip_item_t   *item;
    ngx_http_complex_value_t  *list_value;

    if (definition->func >= NGX_CONDITION_FUNC_LOGIC_FIRST
        && definition->func <= NGX_CONDITION_FUNC_LOGIC_LAST)
    {
        term = definition->terms->elts;

        if (definition->func == NGX_CONDITION_FUNC_NOT) {
            cmp = ngx_http_condition_eval_id(r, cmcf, clcf,
                                             term[0].condition_id, depth);
            if (term[0].negative) {
                cmp = !cmp;
            }

            cmp = !cmp;
            ngx_log_debug5(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition logic, func:%ui ref:%ui negative:%ui "
                           "result:%i depth:%ui",
                           definition->func, term[0].condition_id,
                           term[0].negative, cmp, depth);
            return cmp;
        }

        for (i = 0; i < definition->terms->nelts; i++) {
            cmp = ngx_http_condition_eval_id(r, cmcf, clcf,
                                             term[i].condition_id, depth);
            if (term[i].negative) {
                cmp = !cmp;
            }

            if (definition->func == NGX_CONDITION_FUNC_AND && !cmp) {
                ngx_log_debug6(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "condition logic short circuit, func:%ui "
                               "child:%ui ref:%ui negative:%ui result:%i "
                               "depth:%ui",
                               definition->func, i, term[i].condition_id,
                               term[i].negative, cmp, depth);
                return 0;
            }

            if (definition->func == NGX_CONDITION_FUNC_OR && cmp) {
                ngx_log_debug6(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                               "condition logic short circuit, func:%ui "
                               "child:%ui ref:%ui negative:%ui result:%i "
                               "depth:%ui",
                               definition->func, i, term[i].condition_id,
                               term[i].negative, cmp, depth);
                return 1;
            }
        }

        return definition->func == NGX_CONDITION_FUNC_AND;
    }

    if (definition->func == NGX_CONDITION_FUNC_BOOL) {
        return ngx_http_condition_apply_negation(definition,
                                                 definition->bool_value);
    }

    if (definition->func == NGX_CONDITION_FUNC_TIME_RANGE) {
        return ngx_http_condition_eval_time(r, definition);
    }

    if (ngx_http_complex_value(r, &definition->values[0], &a) != NGX_OK) {
        return 0;
    }

    switch (definition->func) {
    case NGX_CONDITION_FUNC_IS_EMPTY:
        return ngx_http_condition_apply_negation(definition, a.len == 0);
    case NGX_CONDITION_FUNC_IS_NUM:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_is_number(&a));
    case NGX_CONDITION_FUNC_IS_IP:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_parse_ip(&a, &ip) == NGX_OK);
    case NGX_CONDITION_FUNC_IS_CIDR:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_is_cidr(&a));
#if (NGX_CJSON)
    case NGX_CONDITION_FUNC_IS_JSON:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_is_json(&a));
#endif
    case NGX_CONDITION_FUNC_STR_REGEX_MATCH:
#if (NGX_PCRE)
        cmp = ngx_http_regex_exec(r, definition->regex, &a);
        if (cmp == NGX_ERROR) {
            return 0;
        }

        return ngx_http_condition_apply_negation(definition, cmp >= 0);
#else
        return 0;
#endif
    case NGX_CONDITION_FUNC_IP_RANGE:
        if (ngx_condition_parse_ip(&a, &ip) != NGX_OK) {
            return 0;
        }

        item = definition->ip_items->elts;
        for (i = 0; i < definition->ip_items->nelts; i++) {
            if (ngx_condition_ip_item_matches(&ip, &item[i])) {
                return ngx_http_condition_apply_negation(definition, 1);
            }
        }
        return ngx_http_condition_apply_negation(definition, 0);
    default:
        break;
    }

    if (definition->func == NGX_CONDITION_FUNC_STR_IN
        || definition->func == NGX_CONDITION_FUNC_NUM_IN)
    {
        list_value = definition->list_values->elts;

        for (i = 0; i < definition->list_values->nelts; i++) {
            if (ngx_http_complex_value(r, &list_value[i], &b) != NGX_OK) {
                return 0;
            }

            if (definition->func == NGX_CONDITION_FUNC_STR_IN) {
                if (ngx_condition_str_eq(&a, &b, definition->ignore_case)) {
                    return ngx_http_condition_apply_negation(definition, 1);
                }

                continue;
            }

            if (ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK) {
                return 0;
            }

            if (cmp == 0) {
                return ngx_http_condition_apply_negation(definition, 1);
            }
        }

        return ngx_http_condition_apply_negation(definition, 0);
    }

    if (ngx_http_complex_value(r, &definition->values[1], &b) != NGX_OK) {
        return 0;
    }

    switch (definition->func) {
    case NGX_CONDITION_FUNC_STR_EQ:
        return ngx_http_condition_apply_negation(
            definition,
            ngx_condition_str_eq(&a, &b, definition->ignore_case));
    case NGX_CONDITION_FUNC_STR_STARTS_WITH:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_str_starts_with(
                &a, &b, definition->ignore_case));
    case NGX_CONDITION_FUNC_STR_ENDS_WITH:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_str_ends_with(
                &a, &b, definition->ignore_case));
    case NGX_CONDITION_FUNC_STR_CONTAINS:
        return ngx_http_condition_apply_negation(
            definition, ngx_condition_str_contains(
                &a, &b, definition->ignore_case));
    default:
        break;
    }

    if (definition->func == NGX_CONDITION_FUNC_NUM_RANGE) {
        if (definition->values[2].value.data == NULL) {
            zero.len = 1;
            zero.data = (u_char *) "0";

            if (ngx_condition_compare_numbers(&a, &zero, &cmp) != NGX_OK
                || cmp < 0
                || ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK)
            {
                return 0;
            }

            return ngx_http_condition_apply_negation(definition, cmp <= 0);
        }

        if (ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK
            || cmp < 0
            || ngx_http_complex_value(r, &definition->values[2], &b) != NGX_OK
            || ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK)
        {
            return 0;
        }

        return ngx_http_condition_apply_negation(definition, cmp <= 0);
    }

    if (ngx_condition_compare_numbers(&a, &b, &cmp) != NGX_OK) {
        return 0;
    }

    switch (definition->func) {
    case NGX_CONDITION_FUNC_NUM_EQ:
        return ngx_http_condition_apply_negation(definition, cmp == 0);
    case NGX_CONDITION_FUNC_NUM_LT:
        return ngx_http_condition_apply_negation(definition, cmp < 0);
    case NGX_CONDITION_FUNC_NUM_LE:
        return ngx_http_condition_apply_negation(definition, cmp <= 0);
    case NGX_CONDITION_FUNC_NUM_GT:
        return ngx_http_condition_apply_negation(definition, cmp > 0);
    case NGX_CONDITION_FUNC_NUM_GE:
        return ngx_http_condition_apply_negation(definition, cmp >= 0);
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
                       "condition definition, id:%ui item:%ui type:%ui "
                       "result:%i depth:%ui",
                       id, i, entry->type, result, depth);

        if (result) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition definitions OR short circuit, id:%ui "
                           "item:%ui",
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
        type = NGX_CONDITION_FUNC_INVALID;
        if (term[i].condition_id < clcf->effective_nelts
            && clcf->effective[term[i].condition_id] != NULL)
        {
            type = clcf->effective[term[i].condition_id]->type;
        }

        ngx_log_debug7(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "condition expression, expr:%ui term:%ui id:%ui "
                       "name:\"%V\" negative:%ui type:%ui result:%i",
                       expr_id, i, term[i].condition_id,
                       &name[term[i].condition_id].name, term[i].negative,
                       type, result);
#endif

        if (!result) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "condition expression AND short circuit, expr:%ui "
                           "term:%ui",
                           expr_id, i);
            return NGX_CONDITION_EXPR_MISS;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "condition expression matched, expr:%ui", expr_id);
    return NGX_CONDITION_EXPR_HIT;
}


char *
ngx_http_set_conditional_complex_value_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_http_condition_complex_value_ctx_t),
               offsetof(ngx_http_condition_complex_value_ctx_t, value),
               offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
               ngx_http_set_complex_value_slot);
}


char *
ngx_http_set_conditional_complex_value_zero_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_http_condition_complex_value_ctx_t),
               offsetof(ngx_http_condition_complex_value_ctx_t, value),
               offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
               ngx_http_set_complex_value_zero_slot);
}


char *
ngx_http_set_conditional_complex_value_size_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_http_condition_complex_value_ctx_t),
               offsetof(ngx_http_condition_complex_value_ctx_t, value),
               offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
               ngx_http_set_complex_value_size_slot);
}


#if (NGX_RESTY_EXT)

char *
ngx_http_set_conditional_complex_value_msec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_http_condition_complex_value_ctx_t),
               offsetof(ngx_http_condition_complex_value_ctx_t, value),
               offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
               ngx_http_set_complex_value_msec_slot);
}


char *
ngx_http_set_conditional_complex_value_sec_slot(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    return ngx_condition_call_ptr_slot(cf, cmd, conf,
               sizeof(ngx_http_condition_complex_value_ctx_t),
               offsetof(ngx_http_condition_complex_value_ctx_t, value),
               offsetof(ngx_http_condition_complex_value_ctx_t, expr_id),
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
