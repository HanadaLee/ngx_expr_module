#include <ngx_config.h>
#include <ngx_core.h>

#if (NGX_CJSON)
#include <cjson/cJSON.h>
#endif

#include "ngx_condition.h"


typedef void (*ngx_condition_init_value_pt)(void *ctx, size_t value_offset);


typedef struct {
    ngx_uint_t  negative;
    u_char     *integer;
    size_t      integer_len;
    u_char     *fraction;
    size_t      fraction_len;
} ngx_condition_number_t;


static ngx_condition_expr_id_t  ngx_condition_current_expr_id =
    NGX_CONDITION_NO_EXPR_ID;


const ngx_condition_operator_t  ngx_condition_operators[] = {
    /* Canonical entries must follow ngx_condition_op_e order. */
    { ngx_string("not"), NGX_CONDITION_OP_NOT, 1, 1, 0 },
    { ngx_string("and"), NGX_CONDITION_OP_AND, 2, (ngx_uint_t) -1, 0 },
    { ngx_string("or"), NGX_CONDITION_OP_OR, 2, (ngx_uint_t) -1, 0 },
    { ngx_string("bool"), NGX_CONDITION_OP_BOOL, 1, 1, 0 },
    { ngx_string("is_empty"), NGX_CONDITION_OP_IS_EMPTY, 1, 1, 0 },
    { ngx_string("str_eq"), NGX_CONDITION_OP_STR_EQ, 2, 2, 1 },
    { ngx_string("str_starts_with"), NGX_CONDITION_OP_STR_STARTS_WITH,
      2, 2, 1 },
    { ngx_string("str_ends_with"), NGX_CONDITION_OP_STR_ENDS_WITH,
      2, 2, 1 },
    { ngx_string("str_contains"), NGX_CONDITION_OP_STR_CONTAINS,
      2, 2, 1 },
    { ngx_string("str_regex_match"), NGX_CONDITION_OP_STR_REGEX_MATCH,
      2, 2, 1 },
    { ngx_string("str_in"), NGX_CONDITION_OP_STR_IN,
      2, (ngx_uint_t) -1, 1 },
    { ngx_string("is_num"), NGX_CONDITION_OP_IS_NUM, 1, 1, 0 },
    { ngx_string("num_eq"), NGX_CONDITION_OP_NUM_EQ, 2, 2, 0 },
    { ngx_string("num_lt"), NGX_CONDITION_OP_NUM_LT, 2, 2, 0 },
    { ngx_string("num_le"), NGX_CONDITION_OP_NUM_LE, 2, 2, 0 },
    { ngx_string("num_gt"), NGX_CONDITION_OP_NUM_GT, 2, 2, 0 },
    { ngx_string("num_ge"), NGX_CONDITION_OP_NUM_GE, 2, 2, 0 },
    { ngx_string("num_range"), NGX_CONDITION_OP_NUM_RANGE, 2, 3, 0 },
    { ngx_string("num_in"), NGX_CONDITION_OP_NUM_IN,
      2, (ngx_uint_t) -1, 0 },
    { ngx_string("time_range"), NGX_CONDITION_OP_TIME_RANGE, 0, 9, 0 },
    { ngx_string("is_ip"), NGX_CONDITION_OP_IS_IP, 1, 1, 0 },
    { ngx_string("is_cidr"), NGX_CONDITION_OP_IS_CIDR, 1, 1, 0 },
    { ngx_string("ip_range"), NGX_CONDITION_OP_IP_RANGE,
      2, (ngx_uint_t) -1, 0 },
#if (NGX_CJSON)
    { ngx_string("is_json"), NGX_CONDITION_OP_IS_JSON, 1, 1, 0 },
#endif
    /* Symbol aliases follow the canonical, enum-indexed entries. */
    { ngx_string("="), NGX_CONDITION_OP_STR_EQ, 2, 2, 1 },
    { ngx_string("^~"), NGX_CONDITION_OP_STR_STARTS_WITH, 2, 2, 1 },
    { ngx_string("~$"), NGX_CONDITION_OP_STR_ENDS_WITH, 2, 2, 1 },
    { ngx_string("~"), NGX_CONDITION_OP_STR_REGEX_MATCH, 2, 2, 1 },
    { ngx_string("~*"), NGX_CONDITION_OP_STR_REGEX_MATCH, 2, 2, 1 },
    { ngx_string("=="), NGX_CONDITION_OP_NUM_EQ, 2, 2, 0 },
    { ngx_string("<"), NGX_CONDITION_OP_NUM_LT, 2, 2, 0 },
    { ngx_string("<="), NGX_CONDITION_OP_NUM_LE, 2, 2, 0 },
    { ngx_string(">"), NGX_CONDITION_OP_NUM_GT, 2, 2, 0 },
    { ngx_string(">="), NGX_CONDITION_OP_NUM_GE, 2, 2, 0 },
    { ngx_null_string, NGX_CONDITION_OP_INVALID, 0, 0, 0 }
};


static ngx_int_t ngx_condition_copy_str(ngx_pool_t *pool, ngx_str_t *dst,
    ngx_str_t *src);
static void *ngx_condition_prepare_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset, ngx_condition_init_value_pt init,
    ngx_uint_t *created);
static char *ngx_condition_call_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset, ngx_condition_init_value_pt init,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *));
static ngx_int_t ngx_condition_parse_number(ngx_str_t *value,
    ngx_condition_number_t *number);
static ngx_int_t ngx_condition_parse_ipv4(ngx_str_t *value,
    in_addr_t *addr);


const ngx_condition_operator_t *
ngx_condition_find_operator(ngx_str_t *name, ngx_uint_t *negative)
{
    ngx_str_t   base;
    ngx_uint_t  i;

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

    for (i = 0; ngx_condition_operators[i].name.len; i++) {
        if (ngx_condition_operators[i].name.len == base.len
            && ngx_strncmp(ngx_condition_operators[i].name.data,
                           base.data, base.len) == 0)
        {
            if (*negative
                && ngx_condition_operators[i].op
                   <= NGX_CONDITION_OP_LOGIC_LAST)
            {
                return NULL;
            }

            return &ngx_condition_operators[i];
        }
    }

    return NULL;
}


ngx_int_t
ngx_condition_registry_init(ngx_pool_t *pool,
    ngx_condition_registry_t *registry)
{
    if (ngx_array_init(&registry->names, pool, 4,
                       sizeof(ngx_condition_name_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&registry->expressions, pool, 4,
                       sizeof(ngx_condition_when_expr_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_condition_copy_str(ngx_pool_t *pool, ngx_str_t *dst, ngx_str_t *src)
{
    dst->data = ngx_pnalloc(pool, src->len + 1);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(dst->data, src->data, src->len);
    dst->data[src->len] = '\0';
    dst->len = src->len;

    return NGX_OK;
}


ngx_condition_name_t *
ngx_condition_get_or_create_name(ngx_conf_t *cf,
    ngx_condition_registry_t *registry, ngx_str_t *name)
{
    ngx_uint_t             i;
    ngx_condition_name_t  *entry;

    if (name->len == 0 || name->len > NGX_CONDITION_NAME_MAX_LEN
        || name->data[0] == '!')
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid condition name \"%V\"", name);
        return NULL;
    }

    entry = registry->names.elts;

    for (i = 0; i < registry->names.nelts; i++) {
        if (entry[i].name.len == name->len
            && ngx_strncmp(entry[i].name.data, name->data, name->len) == 0)
        {
            return &entry[i];
        }
    }

    if (registry->names.nelts == NGX_CONDITION_NO_ID) {
        return NULL;
    }

    entry = ngx_array_push(&registry->names);
    if (entry == NULL) {
        return NULL;
    }

    ngx_memzero(entry, sizeof(ngx_condition_name_t));

    if (ngx_condition_copy_str(cf->pool, &entry->name, name) != NGX_OK) {
        registry->names.nelts--;
        return NULL;
    }

    entry->id = registry->names.nelts - 1;

    return entry;
}


ngx_int_t
ngx_condition_parse_terms(ngx_conf_t *cf,
    ngx_condition_registry_t *registry, ngx_uint_t first,
    ngx_array_t *terms)
{
    ngx_str_t              name, *value;
    ngx_uint_t             i, negative;
    ngx_condition_term_t  *term;
    ngx_condition_name_t  *entry;

    value = cf->args->elts;

    for (i = first; i < cf->args->nelts; i++) {
        name = value[i];
        negative = (name.len != 0 && name.data[0] == '!');
        if (negative) {
            name.data++;
            name.len--;
        }

        entry = ngx_condition_get_or_create_name(cf, registry, &name);
        if (entry == NULL) {
            return NGX_ERROR;
        }

        term = ngx_array_push(terms);
        if (term == NULL) {
            return NGX_ERROR;
        }

        term->condition_id = entry->id;
        term->negative = negative;
    }

    return NGX_OK;
}


ngx_condition_expr_id_t
ngx_condition_get_when_expr(ngx_conf_t *cf, ngx_condition_registry_t *registry,
    ngx_array_t *terms)
{
    ngx_uint_t                 i;
    ngx_condition_term_t      *a, *b;
    ngx_condition_when_expr_t *expr;

    expr = registry->expressions.elts;

    for (i = 0; i < registry->expressions.nelts; i++) {
        if (expr[i].terms.nelts != terms->nelts) {
            continue;
        }

        a = expr[i].terms.elts;
        b = terms->elts;

        if (ngx_memcmp(a, b, terms->nelts * sizeof(ngx_condition_term_t)) == 0)
        {
            return expr[i].expr_id;
        }
    }

    if (registry->expressions.nelts == NGX_CONDITION_NO_EXPR_ID) {
        return NGX_CONDITION_NO_EXPR_ID;
    }

    expr = ngx_array_push(&registry->expressions);
    if (expr == NULL) {
        return NGX_CONDITION_NO_EXPR_ID;
    }

    expr->expr_id = registry->expressions.nelts - 1;

    if (ngx_array_init(&expr->terms, cf->pool, terms->nelts,
                       sizeof(ngx_condition_term_t)) != NGX_OK)
    {
        registry->expressions.nelts--;
        return NGX_CONDITION_NO_EXPR_ID;
    }

    a = ngx_array_push_n(&expr->terms, terms->nelts);
    if (a == NULL) {
        registry->expressions.nelts--;
        return NGX_CONDITION_NO_EXPR_ID;
    }

    ngx_memcpy(a, terms->elts,
               terms->nelts * sizeof(ngx_condition_term_t));

    return expr->expr_id;
}


ngx_int_t
ngx_condition_validate_names(ngx_conf_t *cf,
    ngx_condition_registry_t *registry)
{
    ngx_uint_t             i;
    ngx_condition_name_t  *name;

    name = registry->names.elts;

    for (i = 0; i < registry->names.nelts; i++) {
        if (!name[i].defined) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "condition \"%V\" is not defined",
                               &name[i].name);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_condition_str_eq(ngx_str_t *a, ngx_str_t *b, ngx_uint_t ignore_case)
{
    if (a->len != b->len) {
        return 0;
    }

    if (a->len == 0) {
        return 1;
    }

    if (ignore_case) {
        return ngx_strncasecmp(a->data, b->data, a->len) == 0;
    }

    return ngx_strncmp(a->data, b->data, a->len) == 0;
}


ngx_int_t
ngx_condition_str_starts_with(ngx_str_t *value, ngx_str_t *prefix,
    ngx_uint_t ignore_case)
{
    ngx_str_t head;

    if (prefix->len > value->len) {
        return 0;
    }

    head.data = value->data;
    head.len = prefix->len;

    return ngx_condition_str_eq(&head, prefix, ignore_case);
}


ngx_int_t
ngx_condition_str_ends_with(ngx_str_t *value, ngx_str_t *suffix,
    ngx_uint_t ignore_case)
{
    ngx_str_t tail;

    if (suffix->len > value->len) {
        return 0;
    }

    tail.data = value->data + value->len - suffix->len;
    tail.len = suffix->len;

    return ngx_condition_str_eq(&tail, suffix, ignore_case);
}


ngx_int_t
ngx_condition_str_contains(ngx_str_t *value, ngx_str_t *part,
    ngx_uint_t ignore_case)
{
    size_t    i;
    ngx_str_t candidate;

    if (part->len == 0) {
        return 1;
    }

    if (part->len > value->len) {
        return 0;
    }

    candidate.len = part->len;

    for (i = 0; i + part->len <= value->len; i++) {
        candidate.data = value->data + i;
        if (ngx_condition_str_eq(&candidate, part, ignore_case)) {
            return 1;
        }
    }

    return 0;
}


static ngx_int_t
ngx_condition_parse_number(ngx_str_t *value,
    ngx_condition_number_t *number)
{
    size_t     i, dot;
    ngx_uint_t digit;

    ngx_memzero(number, sizeof(ngx_condition_number_t));

    if (value->len == 0) {
        return NGX_ERROR;
    }

    i = 0;

    if (value->data[i] == '+' || value->data[i] == '-') {
        number->negative = (value->data[i] == '-');
        i++;
        if (i == value->len) {
            return NGX_ERROR;
        }
    }

    dot = value->len;
    digit = 0;

    for (; i < value->len; i++) {
        if (value->data[i] == '.') {
            if (dot != value->len) {
                return NGX_ERROR;
            }

            dot = i;
            continue;
        }

        if (value->data[i] < '0' || value->data[i] > '9') {
            return NGX_ERROR;
        }

        digit = 1;
    }

    if (!digit) {
        return NGX_ERROR;
    }

    i = (value->data[0] == '+' || value->data[0] == '-') ? 1 : 0;

    while (i < dot && value->data[i] == '0') {
        i++;
    }

    number->integer = value->data + i;
    number->integer_len = dot - i;

    if (dot < value->len) {
        i = value->len;
        while (i > dot + 1 && value->data[i - 1] == '0') {
            i--;
        }

        number->fraction = value->data + dot + 1;
        number->fraction_len = i - dot - 1;
    }

    if (number->integer_len == 0 && number->fraction_len == 0) {
        number->negative = 0;
    }

    return NGX_OK;
}


ngx_int_t
ngx_condition_is_number(ngx_str_t *value)
{
    ngx_condition_number_t number;

    return ngx_condition_parse_number(value, &number) == NGX_OK;
}


ngx_int_t
ngx_condition_compare_numbers(ngx_str_t *a, ngx_str_t *b,
    ngx_int_t *result)
{
    size_t                  i, n;
    ngx_int_t               rc;
    ngx_condition_number_t  na, nb;

    if (ngx_condition_parse_number(a, &na) != NGX_OK
        || ngx_condition_parse_number(b, &nb) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (na.negative != nb.negative) {
        *result = na.negative ? -1 : 1;
        return NGX_OK;
    }

    if (na.integer_len != nb.integer_len) {
        rc = (na.integer_len < nb.integer_len) ? -1 : 1;
        *result = na.negative ? -rc : rc;
        return NGX_OK;
    }

    if (na.integer_len != 0) {
        rc = ngx_memcmp(na.integer, nb.integer, na.integer_len);
        if (rc != 0) {
            rc = (rc < 0) ? -1 : 1;
            *result = na.negative ? -rc : rc;
            return NGX_OK;
        }
    }

    n = ngx_max(na.fraction_len, nb.fraction_len);

    for (i = 0; i < n; i++) {
        u_char da = (i < na.fraction_len) ? na.fraction[i] : '0';
        u_char db = (i < nb.fraction_len) ? nb.fraction[i] : '0';

        if (da != db) {
            rc = (da < db) ? -1 : 1;
            *result = na.negative ? -rc : rc;
            return NGX_OK;
        }
    }

    *result = 0;
    return NGX_OK;
}


ngx_int_t
ngx_condition_parse_uint_range(ngx_str_t *value, ngx_int_t *start,
    ngx_int_t *end)
{
    u_char    *dash;
    ngx_str_t  first, last;

    dash = ngx_strlchr(value->data, value->data + value->len, '-');

    if (dash == NULL) {
        *start = ngx_atoi(value->data, value->len);
        if (*start == NGX_ERROR) {
            return NGX_ERROR;
        }

        *end = *start;
        return NGX_OK;
    }

    first.data = value->data;
    first.len = dash - value->data;
    last.data = dash + 1;
    last.len = value->data + value->len - last.data;

    if (first.len == 0 || last.len == 0) {
        return NGX_ERROR;
    }

    *start = ngx_atoi(first.data, first.len);
    *end = ngx_atoi(last.data, last.len);

    if (*start == NGX_ERROR || *end == NGX_ERROR || *end < *start) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_condition_set_time_range(ngx_conf_t *cf, ngx_condition_range_t *range,
    ngx_str_t *value, ngx_int_t minimum, ngx_int_t maximum,
    const char *field)
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


ngx_int_t
ngx_condition_parse_timezone(ngx_conf_t *cf, ngx_str_t *value,
    ngx_int_t *gmt_offset)
{
    ngx_int_t  hours, minutes, sign;

    if (value->len == 3
        && ngx_strncmp(value->data, "gmt", 3) == 0)
    {
        *gmt_offset = 0;
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
    *gmt_offset = sign * (hours * 3600 + minutes * 60);

    return NGX_OK;
}


ngx_int_t
ngx_condition_range_matches(ngx_condition_range_t *range, ngx_int_t value)
{
    return !range->set || (value >= range->start && value <= range->end);
}


ngx_int_t
ngx_condition_parse_ip(ngx_str_t *value, ngx_condition_ip_t *ip)
{
    ngx_memzero(ip, sizeof(ngx_condition_ip_t));

    if (ngx_condition_parse_ipv4(value, &ip->in) == NGX_OK) {
        ip->family = AF_INET;
        return NGX_OK;
    }

#if (NGX_HAVE_INET6)
    if (ngx_inet6_addr(value->data, value->len, ip->in6) == NGX_OK) {
        ip->family = AF_INET6;
        return NGX_OK;
    }
#endif

    return NGX_ERROR;
}


ngx_int_t
ngx_condition_is_cidr(ngx_str_t *value)
{
    ngx_cidr_t cidr;

    if (ngx_strlchr(value->data, value->data + value->len, '/') == NULL) {
        return 0;
    }

    return ngx_ptocidr(value, &cidr) != NGX_ERROR;
}


static ngx_int_t
ngx_condition_parse_ipv4(ngx_str_t *value, in_addr_t *addr)
{
    *addr = ngx_inet_addr(value->data, value->len);

    if (*addr != INADDR_NONE) {
        return NGX_OK;
    }

    if (value->len == sizeof("255.255.255.255") - 1
        && ngx_strncmp(value->data, "255.255.255.255", value->len) == 0)
    {
        return NGX_OK;
    }

    return NGX_ERROR;
}


ngx_int_t
ngx_condition_parse_ip_item(ngx_str_t *value,
    ngx_condition_ip_item_t *item)
{
    u_char     *dash;
    ngx_int_t   rc;
    ngx_str_t   first, last;
    ngx_cidr_t  cidr;

    ngx_memzero(item, sizeof(ngx_condition_ip_item_t));

    if (value->len == sizeof("255.255.255.255") - 1
        && ngx_strncmp(value->data, "255.255.255.255", value->len) == 0)
    {
        item->family = AF_INET;
        item->addr = INADDR_NONE;
        item->mask = 0xffffffff;
        return NGX_OK;
    }

    dash = ngx_strlchr(value->data, value->data + value->len, '-');
    if (dash != NULL) {
        first.data = value->data;
        first.len = dash - value->data;
        last.data = dash + 1;
        last.len = value->data + value->len - last.data;

        if (first.len == 0 || last.len == 0) {
            return NGX_ERROR;
        }

        if (ngx_condition_parse_ipv4(&first, &item->start) != NGX_OK
            || ngx_condition_parse_ipv4(&last, &item->end) != NGX_OK)
        {
            return NGX_ERROR;
        }

        item->start = ntohl(item->start);
        item->end = ntohl(item->end);
        if (item->end < item->start) {
            return NGX_ERROR;
        }

        item->family = AF_INET;
        item->range = 1;
        return NGX_OK;
    }

    rc = ngx_ptocidr(value, &cidr);
    if (rc != NGX_OK && rc != NGX_DONE) {
        return NGX_ERROR;
    }

    item->family = cidr.family;

    if (cidr.family == AF_INET) {
        item->addr = cidr.u.in.addr;
        item->mask = cidr.u.in.mask;
        return NGX_OK;
    }

#if (NGX_HAVE_INET6)
    if (cidr.family == AF_INET6) {
        ngx_memcpy(item->addr6, cidr.u.in6.addr.s6_addr, 16);
        ngx_memcpy(item->mask6, cidr.u.in6.mask.s6_addr, 16);
        return NGX_OK;
    }
#endif

    return NGX_ERROR;
}


ngx_int_t
ngx_condition_parse_ip_items(ngx_conf_t *cf, ngx_uint_t first,
    ngx_array_t **items)
{
    ngx_str_t                *value;
    ngx_uint_t                i, n;
    ngx_condition_ip_item_t  *item;

    value = cf->args->elts;
    n = cf->args->nelts - first;
    *items = ngx_array_create(cf->pool, n, sizeof(ngx_condition_ip_item_t));
    if (*items == NULL) {
        return NGX_ERROR;
    }

    for (i = first; i < cf->args->nelts; i++) {
        item = ngx_array_push(*items);
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


ngx_int_t
ngx_condition_ip_item_matches(ngx_condition_ip_t *ip,
    ngx_condition_ip_item_t *item)
{
    ngx_uint_t i;

    if (ip->family != item->family) {
        return 0;
    }

    if (item->family == AF_INET) {
        if (item->range) {
            in_addr_t addr = ntohl(ip->in);
            return addr >= item->start && addr <= item->end;
        }

        return (ip->in & item->mask) == item->addr;
    }

#if (NGX_HAVE_INET6)
    if (item->family == AF_INET6) {
        for (i = 0; i < 16; i++) {
            if ((ip->in6[i] & item->mask6[i]) != item->addr6[i]) {
                return 0;
            }
        }
        return 1;
    }
#endif

    return 0;
}


#if (NGX_CJSON)
ngx_int_t
ngx_condition_is_json(ngx_str_t *value)
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


ngx_condition_expr_id_t
ngx_condition_get_current_expr_id(void)
{
    return ngx_condition_current_expr_id;
}


void
ngx_condition_set_current_expr_id(ngx_condition_expr_id_t expr_id)
{
    ngx_condition_current_expr_id = expr_id;
}


ngx_condition_expr_id_t
ngx_condition_get_associated_expr_id(ngx_conf_t *cf)
{
    (void) cf;
    return ngx_condition_current_expr_id;
}


void *
ngx_condition_find_expr_ctx(ngx_array_t *values,
    ngx_condition_expr_id_t expr_id, size_t element_size,
    size_t expr_id_offset)
{
    u_char                   *p;
    ngx_uint_t                i;
    ngx_condition_expr_id_t  *id;

    if (values == NULL || values == NGX_CONF_UNSET_PTR) {
        return NULL;
    }

    p = values->elts;

    for (i = 0; i < values->nelts; i++, p += element_size) {
        id = (ngx_condition_expr_id_t *) (p + expr_id_offset);
        if (*id == expr_id) {
            return p;
        }
    }

    return NULL;
}


void *
ngx_conf_get_conditional_ctx(void *data, ngx_array_t *values,
    size_t element_size, size_t expr_id_offset,
    ngx_condition_eval_pt eval)
{
    u_char                   *p;
    ngx_uint_t                i;
    ngx_condition_expr_id_t  *expr_id;

    if (values == NULL || values == NGX_CONF_UNSET_PTR) {
        return NULL;
    }

    p = values->elts;

    for (i = 0; i < values->nelts; i++, p += element_size) {
        expr_id = (ngx_condition_expr_id_t *) (p + expr_id_offset);

        if (*expr_id == NGX_CONDITION_NO_EXPR_ID
            || eval(data, *expr_id) == NGX_CONDITION_EXPR_HIT)
        {
            return p;
        }
    }

    return NULL;
}


static ngx_uint_t
ngx_condition_array_has_values(ngx_array_t *values)
{
    return values != NULL && values != NGX_CONF_UNSET_PTR
           && values->nelts != 0;
}


static ngx_int_t
ngx_condition_append_array(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *source, size_t element_size)
{
    void         *p;
    ngx_array_t  *copy;

    if (!ngx_condition_array_has_values(source)) {
        return NGX_OK;
    }

    if (source->size != element_size) {
        return NGX_ERROR;
    }

    if (*values == source) {
        copy = ngx_array_create(cf->pool, source->nelts + 1, element_size);
        if (copy == NULL) {
            return NGX_ERROR;
        }

        p = ngx_array_push_n(copy, source->nelts);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(p, source->elts, source->nelts * element_size);
        *values = copy;

        return NGX_OK;
    }

    if (*values == NULL || *values == NGX_CONF_UNSET_PTR) {
        *values = ngx_array_create(cf->pool, source->nelts + 1,
                                   element_size);
        if (*values == NULL) {
            return NGX_ERROR;
        }

    } else if ((*values)->size != element_size) {
        return NGX_ERROR;
    }

    p = ngx_array_push_n(*values, source->nelts);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, source->elts, source->nelts * element_size);

    return NGX_OK;
}


static ngx_int_t
ngx_condition_append_default(ngx_conf_t *cf, ngx_array_t **values,
    size_t element_size, size_t value_offset, size_t value_size,
    size_t expr_id_offset, const void *default_value)
{
    u_char                   *ctx;
    ngx_condition_expr_id_t  *expr_id;

    if (*values == NULL || *values == NGX_CONF_UNSET_PTR) {
        *values = ngx_array_create(cf->pool, 1, element_size);
        if (*values == NULL) {
            return NGX_ERROR;
        }

    } else if ((*values)->size != element_size) {
        return NGX_ERROR;
    }

    ctx = ngx_array_push(*values);
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(ctx, element_size);
    ngx_memcpy(ctx + value_offset, default_value, value_size);

    expr_id = (ngx_condition_expr_id_t *) (ctx + expr_id_offset);
    *expr_id = NGX_CONDITION_NO_EXPR_ID;

    return NGX_OK;
}


static ngx_int_t
ngx_condition_init_conditional_array(ngx_conf_t *cf, ngx_array_t **values,
    size_t element_size, size_t value_offset, size_t value_size,
    size_t expr_id_offset, const void *default_value)
{
    if (ngx_condition_array_has_values(*values)) {
        if ((*values)->size != element_size) {
            return NGX_ERROR;
        }

        if (ngx_condition_find_expr_ctx(*values, NGX_CONDITION_NO_EXPR_ID,
                                        element_size, expr_id_offset)
            != NULL)
        {
            return NGX_OK;
        }
    }

    return ngx_condition_append_default(cf, values, element_size,
                                        value_offset, value_size,
                                        expr_id_offset, default_value);
}


static ngx_int_t
ngx_condition_merge_conditional_array(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, size_t element_size, size_t value_offset,
    size_t value_size, size_t expr_id_offset, const void *default_value)
{
    if (!ngx_condition_array_has_values(*values)) {
        if (ngx_condition_array_has_values(prev)) {
            if (prev->size != element_size) {
                return NGX_ERROR;
            }

            if (ngx_condition_find_expr_ctx(prev, NGX_CONDITION_NO_EXPR_ID,
                                            element_size, expr_id_offset)
                != NULL)
            {
                *values = prev;
                return NGX_OK;
            }

            if (ngx_condition_append_array(cf, values, prev, element_size)
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        return ngx_condition_init_conditional_array(cf, values,
                   element_size, value_offset, value_size, expr_id_offset,
                   default_value);
    }

    if ((*values)->size != element_size) {
        return NGX_ERROR;
    }

    if (ngx_condition_find_expr_ctx(*values, NGX_CONDITION_NO_EXPR_ID,
                                    element_size, expr_id_offset)
        != NULL)
    {
        return NGX_OK;
    }

    if (ngx_condition_append_array(cf, values, prev, element_size) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_condition_init_conditional_array(cf, values, element_size,
               value_offset, value_size, expr_id_offset, default_value);
}


#define ngx_condition_merge_helpers(name, type, ctx_type)                    \
    ngx_int_t                                                                \
    ngx_conf_init_conditional_##name##_value(ngx_conf_t *cf,                 \
        ngx_array_t **values, type default_value)                            \
    {                                                                        \
        return ngx_condition_init_conditional_array(cf, values,              \
                   sizeof(ctx_type), offsetof(ctx_type, value),              \
                   sizeof(type), offsetof(ctx_type, expr_id),                \
                   &default_value);                                          \
    }                                                                        \
                                                                             \
                                                                             \
    ngx_int_t                                                                \
    ngx_conf_merge_conditional_##name##_value(ngx_conf_t *cf,                \
        ngx_array_t **values, ngx_array_t *prev, type default_value)         \
    {                                                                        \
        return ngx_condition_merge_conditional_array(cf, values, prev,       \
                   sizeof(ctx_type), offsetof(ctx_type, value),              \
                   sizeof(type), offsetof(ctx_type, expr_id),                \
                   &default_value);                                          \
    }


ngx_condition_merge_helpers(flag, ngx_flag_t,
    ngx_conf_condition_flag_ctx_t)
ngx_condition_merge_helpers(str, ngx_str_t,
    ngx_conf_condition_str_ctx_t)
ngx_condition_merge_helpers(num, ngx_int_t,
    ngx_conf_condition_num_ctx_t)
ngx_condition_merge_helpers(size, size_t,
    ngx_conf_condition_size_ctx_t)
ngx_condition_merge_helpers(off, off_t,
    ngx_conf_condition_off_ctx_t)
ngx_condition_merge_helpers(msec, ngx_msec_t,
    ngx_conf_condition_msec_ctx_t)
ngx_condition_merge_helpers(sec, time_t,
    ngx_conf_condition_sec_ctx_t)
ngx_condition_merge_helpers(enum, ngx_uint_t,
    ngx_conf_condition_enum_ctx_t)
ngx_condition_merge_helpers(bitmask, ngx_uint_t,
    ngx_conf_condition_bitmask_ctx_t)

#undef ngx_condition_merge_helpers


static size_t
ngx_condition_ptr_ctx_size(ngx_array_t *values, ngx_array_t *prev)
{
    if (values != NULL && values != NGX_CONF_UNSET_PTR) {
        return values->size;
    }

    if (prev != NULL && prev != NGX_CONF_UNSET_PTR) {
        return prev->size;
    }

    return sizeof(ngx_conf_condition_ptr_ctx_t);
}


ngx_int_t
ngx_conf_init_conditional_ptr_value(ngx_conf_t *cf, ngx_array_t **values,
    void *default_value)
{
    size_t  element_size;

    element_size = ngx_condition_ptr_ctx_size(*values, NULL);

    return ngx_condition_init_conditional_array(cf, values, element_size,
               offsetof(ngx_conf_condition_ptr_ctx_t, value),
               sizeof(void *),
               offsetof(ngx_conf_condition_ptr_ctx_t, expr_id),
               &default_value);
}


ngx_int_t
ngx_conf_merge_conditional_ptr_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, void *default_value)
{
    size_t  element_size, value_offset, expr_id_offset;

    element_size = ngx_condition_ptr_ctx_size(*values, prev);
    value_offset = offsetof(ngx_conf_condition_ptr_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_condition_ptr_ctx_t, expr_id);

    return ngx_condition_merge_conditional_array(cf, values, prev,
                                                 element_size,
                                                 value_offset,
                                                 sizeof(void *),
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_conditional_bufs_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_uint_t default_num, size_t default_size)
{
    ngx_bufs_t default_value;

    default_value.num = default_num;
    default_value.size = default_size;

    return ngx_condition_init_conditional_array(cf, values,
               sizeof(ngx_conf_condition_bufs_ctx_t),
               offsetof(ngx_conf_condition_bufs_ctx_t, value),
               sizeof(ngx_bufs_t),
               offsetof(ngx_conf_condition_bufs_ctx_t, expr_id),
               &default_value);
}


ngx_int_t
ngx_conf_merge_conditional_bufs_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_uint_t default_num, size_t default_size)
{
    ngx_bufs_t default_value;

    default_value.num = default_num;
    default_value.size = default_size;

    return ngx_condition_merge_conditional_array(cf, values, prev,
               sizeof(ngx_conf_condition_bufs_ctx_t),
               offsetof(ngx_conf_condition_bufs_ctx_t, value),
               sizeof(ngx_bufs_t),
               offsetof(ngx_conf_condition_bufs_ctx_t, expr_id),
               &default_value);
}


static void *
ngx_condition_prepare_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    size_t element_size, size_t value_offset, size_t expr_id_offset,
    ngx_condition_init_value_pt init, ngx_uint_t *created)
{
    void                     *ctx;
    ngx_array_t             **array;
    ngx_condition_expr_id_t  *expr_id;

    array = (ngx_array_t **) ((u_char *) conf + cmd->offset);

    if (*array == NULL || *array == NGX_CONF_UNSET_PTR) {
        *array = ngx_array_create(cf->pool, 2, element_size);
        if (*array == NULL) {
            return NULL;
        }
    }

    ctx = ngx_condition_find_expr_ctx(*array,
              ngx_condition_current_expr_id, element_size, expr_id_offset);
    if (ctx != NULL) {
        *created = 0;
        return ctx;
    }

    ctx = ngx_array_push(*array);
    if (ctx == NULL) {
        return NULL;
    }

    ngx_memzero(ctx, element_size);
    init(ctx, value_offset);

    expr_id = (ngx_condition_expr_id_t *)
                  ((u_char *) ctx + expr_id_offset);
    *expr_id = ngx_condition_current_expr_id;
    *created = 1;

    return ctx;
}


static char *
ngx_condition_call_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    size_t element_size, size_t value_offset, size_t expr_id_offset,
    ngx_condition_init_value_pt init,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *))
{
    char          *rv;
    void          *ctx;
    ngx_uint_t     created;
    ngx_array_t  **array;
    ngx_command_t  local_cmd;

    created = 0;
    ctx = ngx_condition_prepare_slot(cf, cmd, conf, element_size,
                                     value_offset, expr_id_offset, init,
                                     &created);
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    local_cmd = *cmd;
    local_cmd.offset = value_offset;

    rv = setter(cf, &local_cmd, ctx);
    if (rv != NGX_CONF_OK && created) {
        array = (ngx_array_t **) ((u_char *) conf + cmd->offset);
        (*array)->nelts--;
    }

    return rv;
}


#define ngx_condition_init_scalar(name, type, initial)                       \
    static void                                                              \
    name(void *data, size_t value_offset)                                    \
    {                                                                        \
        type *value = (type *) ((u_char *) data + value_offset);             \
        *value = initial;                                                    \
    }

ngx_condition_init_scalar(ngx_condition_init_flag,
    ngx_flag_t, NGX_CONF_UNSET)
ngx_condition_init_scalar(ngx_condition_init_num,
    ngx_int_t, NGX_CONF_UNSET)
ngx_condition_init_scalar(ngx_condition_init_size,
    size_t, NGX_CONF_UNSET_SIZE)
ngx_condition_init_scalar(ngx_condition_init_off,
    off_t, NGX_CONF_UNSET)
ngx_condition_init_scalar(ngx_condition_init_msec,
    ngx_msec_t, NGX_CONF_UNSET_MSEC)
ngx_condition_init_scalar(ngx_condition_init_sec,
    time_t, NGX_CONF_UNSET)
ngx_condition_init_scalar(ngx_condition_init_enum,
    ngx_uint_t, NGX_CONF_UNSET_UINT)
ngx_condition_init_scalar(ngx_condition_init_bitmask,
    ngx_uint_t, 0)


static void
ngx_condition_init_str(void *data, size_t value_offset)
{
    ngx_str_t  *value;

    value = (ngx_str_t *) ((u_char *) data + value_offset);
    value->len = 0;
    value->data = NULL;
}


static void
ngx_condition_init_ptr(void *data, size_t value_offset)
{
    void  **value;

    value = (void **) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET_PTR;
}


static void
ngx_condition_init_bufs(void *data, size_t value_offset)
{
    ngx_bufs_t  *value;

    value = (ngx_bufs_t *) ((u_char *) data + value_offset);
    ngx_memzero(value, sizeof(ngx_bufs_t));
}


char *
ngx_condition_call_ptr_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *))
{
    return ngx_condition_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_condition_init_ptr,
                                   setter);
}


char *
ngx_conf_set_conditional_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_flag_ctx_t),
                                   offsetof(ngx_conf_condition_flag_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_flag_ctx_t,
                                            expr_id),
                                   ngx_condition_init_flag,
                                   ngx_conf_set_flag_slot);
}


char *
ngx_conf_set_conditional_str_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_str_ctx_t),
                                   offsetof(ngx_conf_condition_str_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_str_ctx_t,
                                            expr_id),
                                   ngx_condition_init_str,
                                   ngx_conf_set_str_slot);
}


char *
ngx_conf_set_conditional_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_str_array_ctx_t),
                                   offsetof(ngx_conf_condition_str_array_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_str_array_ctx_t,
                                            expr_id),
                                   ngx_condition_init_ptr,
                                   ngx_conf_set_str_array_slot);
}


char *
ngx_conf_set_conditional_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_keyval_ctx_t),
                                   offsetof(ngx_conf_condition_keyval_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_keyval_ctx_t,
                                            expr_id),
                                   ngx_condition_init_ptr,
                                   ngx_conf_set_keyval_slot);
}


char *
ngx_conf_set_conditional_num_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_num_ctx_t),
                                   offsetof(ngx_conf_condition_num_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_num_ctx_t,
                                            expr_id),
                                   ngx_condition_init_num,
                                   ngx_conf_set_num_slot);
}


char *
ngx_conf_set_conditional_size_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_size_ctx_t),
                                   offsetof(ngx_conf_condition_size_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_size_ctx_t,
                                            expr_id),
                                   ngx_condition_init_size,
                                   ngx_conf_set_size_slot);
}


char *
ngx_conf_set_conditional_off_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_off_ctx_t),
                                   offsetof(ngx_conf_condition_off_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_off_ctx_t,
                                            expr_id),
                                   ngx_condition_init_off,
                                   ngx_conf_set_off_slot);
}


char *
ngx_conf_set_conditional_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_msec_ctx_t),
                                   offsetof(ngx_conf_condition_msec_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_msec_ctx_t,
                                            expr_id),
                                   ngx_condition_init_msec,
                                   ngx_conf_set_msec_slot);
}


char *
ngx_conf_set_conditional_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_sec_ctx_t),
                                   offsetof(ngx_conf_condition_sec_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_sec_ctx_t,
                                            expr_id),
                                   ngx_condition_init_sec,
                                   ngx_conf_set_sec_slot);
}


char *
ngx_conf_set_conditional_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_bufs_ctx_t),
                                   offsetof(ngx_conf_condition_bufs_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_bufs_ctx_t,
                                            expr_id),
                                   ngx_condition_init_bufs,
                                   ngx_conf_set_bufs_slot);
}


char *
ngx_conf_set_conditional_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_enum_ctx_t),
                                   offsetof(ngx_conf_condition_enum_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_enum_ctx_t,
                                            expr_id),
                                   ngx_condition_init_enum,
                                   ngx_conf_set_enum_slot);
}


char *
ngx_conf_set_conditional_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_condition_call_slot(cf, cmd, conf,
                                   sizeof(ngx_conf_condition_bitmask_ctx_t),
                                   offsetof(ngx_conf_condition_bitmask_ctx_t,
                                            value),
                                   offsetof(ngx_conf_condition_bitmask_ctx_t,
                                            expr_id),
                                   ngx_condition_init_bitmask,
                                   ngx_conf_set_bitmask_slot);
}
