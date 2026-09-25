
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>

#if (nginx_version >= 1031005)
#include <ngx_json_parse.h>
#elif (NGX_CJSON)
#include <cjson/cJSON.h>
#endif

#include "ngx_expr.h"


typedef void (*ngx_expr_init_value_pt)(void *ctx, size_t value_offset);


typedef struct {
    ngx_uint_t                     negative;
    u_char                         *integer;
    size_t                         integer_len;
    u_char                         *fraction;
    size_t                         fraction_len;
} ngx_expr_number_t;


typedef struct {
    ngx_rbtree_node_t              node;
    u_char                         start[NGX_EXPR_IP_KEY_LEN];
    u_char                         end[NGX_EXPR_IP_KEY_LEN];
    u_char                         max_end[NGX_EXPR_IP_KEY_LEN];
} ngx_expr_ip_range_node_t;


typedef struct {
    ngx_rbtree_t                   tree;
    ngx_rbtree_node_t              sentinel;
    size_t                         key_len;
} ngx_expr_ip_range_tree_t;


struct ngx_expr_ip_ranges_s {
    ngx_array_t                    nodes;
    ngx_expr_ip_range_tree_t       ipv4;
    ngx_expr_ip_range_tree_t       ipv6;
};


static ngx_int_t ngx_expr_copy_str(ngx_pool_t *pool, ngx_str_t *dst,
    ngx_str_t *src);
static void *ngx_expr_prepare_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset, ngx_expr_init_value_pt init,
    ngx_uint_t *created);
static char *ngx_expr_call_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset, ngx_expr_init_value_pt init,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *));
static ngx_int_t ngx_expr_parse_number(ngx_str_t *value,
    ngx_expr_number_t *number);
static ngx_int_t ngx_expr_parse_ipv4(ngx_str_t *value, in_addr_t *addr);
static void ngx_expr_ip_range_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
static void ngx_expr_ip_range_update_max(ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel, size_t key_len);
static ngx_int_t ngx_expr_ip_range_tree_match(
    ngx_expr_ip_range_tree_t *tree, u_char *address);
static ngx_int_t ngx_expr_ip_range_tree_match_node(
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel, u_char *address,
    size_t key_len);


static ngx_expr_when_id_t  ngx_expr_current_when_id = NGX_EXPR_NO_WHEN_ID;


ngx_int_t
ngx_expr_registry_init(ngx_pool_t *pool, ngx_expr_registry_t *registry)
{
    if (ngx_array_init(&registry->names, pool, 4,
                       sizeof(ngx_expr_name_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&registry->expressions, pool, 4,
                       sizeof(ngx_expr_when_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_expr_copy_str(ngx_pool_t *pool, ngx_str_t *dst, ngx_str_t *src)
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


ngx_expr_name_t *
ngx_expr_get_name(ngx_conf_t *cf,
    ngx_expr_registry_t *registry, ngx_str_t *name)
{
    ngx_uint_t              i;
    ngx_expr_name_t         *entry;

    if (name->len == 0 || name->len > NGX_EXPR_NAME_MAX_LEN
        || name->data[0] == '!')
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid expr name \"%V\"", name);
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

    if (registry->names.nelts == NGX_EXPR_NO_ID) {
        return NULL;
    }

    entry = ngx_array_push(&registry->names);
    if (entry == NULL) {
        return NULL;
    }

    ngx_memzero(entry, sizeof(ngx_expr_name_t));

    if (ngx_expr_copy_str(cf->pool, &entry->name, name) != NGX_OK) {
        registry->names.nelts--;
        return NULL;
    }

    entry->id = registry->names.nelts - 1;

    return entry;
}


ngx_int_t
ngx_expr_parse_terms(ngx_conf_t *cf,
    ngx_expr_registry_t *registry, ngx_uint_t first,
    ngx_array_t *terms)
{
    ngx_str_t               name, *value;
    ngx_uint_t              i, negative;
    ngx_expr_term_t         *term;
    ngx_expr_name_t         *entry;

    value = cf->args->elts;

    for (i = first; i < cf->args->nelts; i++) {
        name = value[i];
        negative = (name.len != 0 && name.data[0] == '!');
        if (negative) {
            name.data++;
            name.len--;
        }

        entry = ngx_expr_get_name(cf, registry, &name);
        if (entry == NULL) {
            return NGX_ERROR;
        }

        term = ngx_array_push(terms);
        if (term == NULL) {
            return NGX_ERROR;
        }

        term->expr_id = entry->id;
        term->negative = negative;
    }

    return NGX_OK;
}


ngx_expr_when_id_t
ngx_expr_get_when_id(ngx_conf_t *cf, ngx_expr_registry_t *registry,
    ngx_array_t *terms)
{
    ngx_uint_t                   i;
    ngx_expr_term_t              *a, *b;
    ngx_expr_when_t              *expr;

    expr = registry->expressions.elts;

    for (i = 0; i < registry->expressions.nelts; i++) {
        if (expr[i].terms.nelts != terms->nelts) {
            continue;
        }

        a = expr[i].terms.elts;
        b = terms->elts;

        if (ngx_memcmp(a, b, terms->nelts * sizeof(ngx_expr_term_t)) == 0)
        {
            return expr[i].expr_id;
        }
    }

    if (registry->expressions.nelts == NGX_EXPR_NO_WHEN_ID) {
        return NGX_EXPR_NO_WHEN_ID;
    }

    expr = ngx_array_push(&registry->expressions);
    if (expr == NULL) {
        return NGX_EXPR_NO_WHEN_ID;
    }

    expr->expr_id = registry->expressions.nelts - 1;

    if (ngx_array_init(&expr->terms, cf->pool, terms->nelts,
                       sizeof(ngx_expr_term_t)) != NGX_OK)
    {
        registry->expressions.nelts--;
        return NGX_EXPR_NO_WHEN_ID;
    }

    a = ngx_array_push_n(&expr->terms, terms->nelts);
    if (a == NULL) {
        registry->expressions.nelts--;
        return NGX_EXPR_NO_WHEN_ID;
    }

    ngx_memcpy(a, terms->elts, terms->nelts * sizeof(ngx_expr_term_t));

    return expr->expr_id;
}


ngx_int_t
ngx_expr_validate_names(ngx_conf_t *cf, ngx_expr_registry_t *registry)
{
    ngx_uint_t              i;
    ngx_expr_name_t         *name;

    name = registry->names.elts;

    for (i = 0; i < registry->names.nelts; i++) {
        if (!name[i].defined) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "expr \"%V\" is not defined",
                               &name[i].name);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_expr_str_eq(ngx_str_t *a, ngx_str_t *b, ngx_uint_t ignore_case)
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
ngx_expr_str_starts_with(ngx_str_t *value, ngx_str_t *prefix,
    ngx_uint_t ignore_case)
{
    ngx_str_t   head;

    if (prefix->len > value->len) {
        return 0;
    }

    head.data = value->data;
    head.len = prefix->len;

    return ngx_expr_str_eq(&head, prefix, ignore_case);
}


ngx_int_t
ngx_expr_str_ends_with(ngx_str_t *value, ngx_str_t *suffix,
    ngx_uint_t ignore_case)
{
    ngx_str_t   tail;

    if (suffix->len > value->len) {
        return 0;
    }

    tail.data = value->data + value->len - suffix->len;
    tail.len = suffix->len;

    return ngx_expr_str_eq(&tail, suffix, ignore_case);
}


ngx_int_t
ngx_expr_str_contains(ngx_str_t *value, ngx_str_t *part, ngx_uint_t ignore_case)
{
    size_t      i;
    ngx_str_t   candidate;

    if (part->len == 0) {
        return 1;
    }

    if (part->len > value->len) {
        return 0;
    }

    candidate.len = part->len;

    for (i = 0; i + part->len <= value->len; i++) {
        candidate.data = value->data + i;
        if (ngx_expr_str_eq(&candidate, part, ignore_case)) {
            return 1;
        }
    }

    return 0;
}


static ngx_int_t
ngx_expr_parse_number(ngx_str_t *value, ngx_expr_number_t *number)
{
    size_t       i, dot;
    ngx_uint_t   digit;

    ngx_memzero(number, sizeof(ngx_expr_number_t));

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
ngx_expr_is_number(ngx_str_t *value)
{
    ngx_expr_number_t  number;

    return ngx_expr_parse_number(value, &number) == NGX_OK;
}


ngx_int_t
ngx_expr_compare_numbers(ngx_str_t *a, ngx_str_t *b, ngx_int_t *result)
{
    size_t                   i, n;
    ngx_int_t                rc;
    ngx_expr_number_t        na, nb;
    u_char                   da, db;

    if (ngx_expr_parse_number(a, &na) != NGX_OK
        || ngx_expr_parse_number(b, &nb) != NGX_OK)
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
        da = (i < na.fraction_len) ? na.fraction[i] : '0';
        db = (i < nb.fraction_len) ? nb.fraction[i] : '0';

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
ngx_expr_parse_uint_range(ngx_str_t *value, ngx_int_t *start, ngx_int_t *end)
{
    u_char      *dash;
    ngx_str_t   first, last;

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
ngx_expr_set_time_range(ngx_conf_t *cf, ngx_expr_range_t *range,
    ngx_str_t *value, ngx_int_t minimum, ngx_int_t maximum,
    const char *field)
{
    if (range->set
        || ngx_expr_parse_uint_range(value, &range->start,
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
ngx_expr_parse_timezone(ngx_conf_t *cf, ngx_str_t *value, ngx_int_t *gmt_offset)
{
    ngx_int_t   hours, minutes, sign;

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
ngx_expr_range_matches(ngx_expr_range_t *range, ngx_int_t value)
{
    return !range->set || (value >= range->start && value <= range->end);
}


ngx_int_t
ngx_expr_parse_ip(ngx_str_t *value, ngx_expr_ip_t *ip)
{
    ngx_memzero(ip, sizeof(ngx_expr_ip_t));

    if (ngx_expr_parse_ipv4(value, &ip->in) == NGX_OK) {
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
ngx_expr_is_cidr(ngx_str_t *value)
{
    ngx_cidr_t   cidr;

    if (ngx_strlchr(value->data, value->data + value->len, '/') == NULL) {
        return 0;
    }

    return ngx_ptocidr(value, &cidr) != NGX_ERROR;
}


static ngx_int_t
ngx_expr_parse_ipv4(ngx_str_t *value, in_addr_t *addr)
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
ngx_expr_parse_ip_item(ngx_str_t *value, ngx_expr_ip_item_t *item)
{
    u_char               *dash;
    ngx_int_t            rc;
    ngx_str_t            first, last;
    ngx_cidr_t           cidr;
    ngx_expr_ip_t        ip;

    ngx_memzero(item, sizeof(ngx_expr_ip_item_t));

    if (ngx_expr_parse_ip(value, &ip) == NGX_OK) {
        item->family = ip.family;

        if (ip.family == AF_INET) {
            item->addr = ip.in;
            item->mask = 0xffffffff;
            return NGX_OK;
        }

#if (NGX_HAVE_INET6)
        if (ip.family == AF_INET6) {
            ngx_memcpy(item->addr6, ip.in6, 16);
            ngx_memset(item->mask6, 0xff, 16);
            return NGX_OK;
        }
#endif

        return NGX_ERROR;
    }

    if (ngx_strlchr(value->data, value->data + value->len, '/') != NULL) {
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

    dash = ngx_strlchr(value->data, value->data + value->len, '-');
    if (dash != NULL) {
        first.data = value->data;
        first.len = dash - value->data;
        last.data = dash + 1;
        last.len = value->data + value->len - last.data;

        if (first.len == 0 || last.len == 0) {
            return NGX_ERROR;
        }

        if (ngx_expr_parse_ipv4(&first, &item->start) != NGX_OK
            || ngx_expr_parse_ipv4(&last, &item->end) != NGX_OK)
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

    return NGX_ERROR;
}


ngx_int_t
ngx_expr_parse_ip_items(ngx_conf_t *cf, ngx_uint_t first, ngx_array_t **items)
{
    ngx_str_t                  *value;
    ngx_uint_t                 i, n;
    ngx_expr_ip_item_t         *item;

    value = cf->args->elts;
    n = cf->args->nelts - first;
    *items = ngx_array_create(cf->pool, n, sizeof(ngx_expr_ip_item_t));
    if (*items == NULL) {
        return NGX_ERROR;
    }

    for (i = first; i < cf->args->nelts; i++) {
        item = ngx_array_push(*items);
        if (item == NULL) {
            return NGX_ERROR;
        }

        if (ngx_expr_parse_ip_item(&value[i], item) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid IP item \"%V\"", &value[i]);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_expr_ip_range_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_int_t                      rc;
    ngx_rbtree_node_t              **p;
    ngx_expr_ip_range_node_t       *range, *current;

    range = ngx_rbtree_data(node, ngx_expr_ip_range_node_t, node);

    for ( ;; ) {
        current = ngx_rbtree_data(temp, ngx_expr_ip_range_node_t, node);

        rc = ngx_memcmp(range->start, current->start, NGX_EXPR_IP_KEY_LEN);
        if (rc == 0) {
            rc = ngx_memcmp(range->end, current->end, NGX_EXPR_IP_KEY_LEN);
        }

        if (rc < 0) {
            p = &temp->left;

        } else {
            p = &temp->right;
        }

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


static void
ngx_expr_ip_range_update_max(ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel, size_t key_len)
{
    ngx_expr_ip_range_node_t *range, *child;

    if (node == sentinel) {
        return;
    }

    ngx_expr_ip_range_update_max(node->left, sentinel, key_len);
    ngx_expr_ip_range_update_max(node->right, sentinel, key_len);

    range = ngx_rbtree_data(node, ngx_expr_ip_range_node_t, node);
    ngx_memcpy(range->max_end, range->end, key_len);

    if (node->left != sentinel) {
        child = ngx_rbtree_data(node->left, ngx_expr_ip_range_node_t, node);
        if (ngx_memcmp(child->max_end, range->max_end, key_len) > 0) {
            ngx_memcpy(range->max_end, child->max_end, key_len);
        }
    }

    if (node->right != sentinel) {
        child = ngx_rbtree_data(node->right, ngx_expr_ip_range_node_t, node);
        if (ngx_memcmp(child->max_end, range->max_end, key_len) > 0) {
            ngx_memcpy(range->max_end, child->max_end, key_len);
        }
    }
}


static ngx_int_t
ngx_expr_ip_range_tree_match_node(ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel, u_char *address, size_t key_len)
{
    ngx_int_t                      rc;
    ngx_expr_ip_range_node_t       *range, *left;

    if (node == sentinel) {
        return 0;
    }

    range = ngx_rbtree_data(node, ngx_expr_ip_range_node_t, node);

    if (node->left != sentinel) {
        left = ngx_rbtree_data(node->left, ngx_expr_ip_range_node_t, node);
        if (ngx_memcmp(left->max_end, address, key_len) >= 0
            && ngx_expr_ip_range_tree_match_node(node->left, sentinel,
                                                      address, key_len))
        {
            return 1;
        }
    }

    rc = ngx_memcmp(address, range->start, key_len);
    if (rc >= 0
        && ngx_memcmp(address, range->end, key_len) <= 0)
    {
        return 1;
    }

    if (rc < 0) {
        return 0;
    }

    return ngx_expr_ip_range_tree_match_node(node->right, sentinel,
                                                  address, key_len);
}


static ngx_int_t
ngx_expr_ip_range_tree_match(ngx_expr_ip_range_tree_t *tree, u_char *address)
{
    if (tree->tree.root == tree->tree.sentinel) {
        return 0;
    }

    return ngx_expr_ip_range_tree_match_node(
               tree->tree.root, tree->tree.sentinel, address, tree->key_len);
}


ngx_int_t
ngx_expr_parse_ip_ranges(ngx_conf_t *cf, ngx_uint_t first,
    ngx_expr_ip_ranges_t **ranges)
{
    ngx_str_t                      *value;
    ngx_uint_t                     i, n;
    ngx_uint_t                     j;
    in_addr_t                      start, end;
    ngx_expr_ip_item_t             item;
    ngx_expr_ip_range_node_t       *node;
    ngx_expr_ip_ranges_t           *ip_ranges;

    value = cf->args->elts;
    n = cf->args->nelts - first;

    ip_ranges = ngx_pcalloc(cf->pool, sizeof(ngx_expr_ip_ranges_t));
    if (ip_ranges == NULL) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ip_ranges->nodes, cf->pool, n,
                       sizeof(ngx_expr_ip_range_node_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ip_ranges->ipv4.key_len = sizeof(in_addr_t);
    ip_ranges->ipv6.key_len = 16;
    ngx_rbtree_init(&ip_ranges->ipv4.tree, &ip_ranges->ipv4.sentinel,
                    ngx_expr_ip_range_insert_value);
    ngx_rbtree_init(&ip_ranges->ipv6.tree, &ip_ranges->ipv6.sentinel,
                    ngx_expr_ip_range_insert_value);

    for (i = first; i < cf->args->nelts; i++) {
        if (ngx_expr_parse_ip_item(&value[i], &item) != NGX_OK) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid IP item \"%V\"", &value[i]);
            return NGX_ERROR;
        }

        node = ngx_array_push(&ip_ranges->nodes);
        if (node == NULL) {
            return NGX_ERROR;
        }

        ngx_memzero(node, sizeof(ngx_expr_ip_range_node_t));

        if (item.family == AF_INET) {
            if (item.range) {
                start = item.start;
                end = item.end;

            } else {
                start = ntohl(item.addr & item.mask);
                end = start | ntohl((uint32_t) ~item.mask);
            }

            start = htonl((uint32_t) start);
            end = htonl((uint32_t) end);
            ngx_memcpy(node->start, &start, sizeof(in_addr_t));
            ngx_memcpy(node->end, &end, sizeof(in_addr_t));
            ngx_rbtree_insert(&ip_ranges->ipv4.tree, &node->node);
            continue;
        }

#if (NGX_HAVE_INET6)
        if (item.family == AF_INET6) {
            for (j = 0; j < 16; j++) {
                node->start[j] = item.addr6[j] & item.mask6[j];
                node->end[j] = node->start[j]
                               | (u_char) ~item.mask6[j];
            }

            ngx_rbtree_insert(&ip_ranges->ipv6.tree, &node->node);
            continue;
        }
#endif

        return NGX_ERROR;
    }

    ngx_expr_ip_range_update_max(ip_ranges->ipv4.tree.root,
                                      ip_ranges->ipv4.tree.sentinel,
                                      ip_ranges->ipv4.key_len);
    ngx_expr_ip_range_update_max(ip_ranges->ipv6.tree.root,
                                      ip_ranges->ipv6.tree.sentinel,
                                      ip_ranges->ipv6.key_len);

    *ranges = ip_ranges;

    return NGX_OK;
}


ngx_int_t
ngx_expr_ip_item_matches(ngx_expr_ip_t *ip, ngx_expr_ip_item_t *item)
{
    ngx_uint_t   i;

    if (ip->family != item->family) {
        return 0;
    }

    if (item->family == AF_INET) {
        if (item->range) {
            in_addr_t   addr = ntohl(ip->in);
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


ngx_int_t
ngx_expr_ip_ranges_match(ngx_expr_ip_t *ip, ngx_expr_ip_ranges_t *ranges)
{
    u_char   address[NGX_EXPR_IP_KEY_LEN];

    if (ranges == NULL) {
        return 0;
    }

    ngx_memzero(address, sizeof(address));

    if (ip->family == AF_INET) {
        ngx_memcpy(address, &ip->in, sizeof(in_addr_t));
        return ngx_expr_ip_range_tree_match(&ranges->ipv4, address);
    }

#if (NGX_HAVE_INET6)
    if (ip->family == AF_INET6) {
        ngx_memcpy(address, ip->in6, 16);
        return ngx_expr_ip_range_tree_match(&ranges->ipv6, address);
    }
#endif

    return 0;
}


#if (nginx_version >= 1031005 || NGX_CJSON)

ngx_int_t
ngx_expr_is_json(ngx_pool_t *pool, ngx_str_t *value)
{
#if (nginx_version >= 1031005)

    return ngx_json_parse(pool, value, NULL, NULL) == NGX_OK;

#else
    const char   *end;
    cJSON        *json;

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
#endif
}

#endif


ngx_expr_when_id_t
ngx_expr_get_current_when_id(void)
{
    return ngx_expr_current_when_id;
}


void
ngx_expr_set_current_when_id(ngx_expr_when_id_t expr_id)
{
    ngx_expr_current_when_id = expr_id;
}


ngx_expr_when_id_t
ngx_expr_get_associated_when_id(ngx_conf_t *cf)
{
    (void) cf;
    return ngx_expr_current_when_id;
}


void *
ngx_expr_find_ctx(ngx_array_t *values,
    ngx_expr_when_id_t expr_id, size_t element_size,
    size_t expr_id_offset)
{
    u_char                     *p;
    ngx_uint_t                 i;
    ngx_expr_when_id_t         *id;

    if (values == NULL || values == NGX_CONF_UNSET_PTR) {
        return NULL;
    }

    p = values->elts;

    for (i = 0; i < values->nelts; i++, p += element_size) {
        id = (ngx_expr_when_id_t *) (p + expr_id_offset);
        if (*id == expr_id) {
            return p;
        }
    }

    return NULL;
}


void *
ngx_conf_get_expr_ctx(void *data, ngx_array_t *values,
                      size_t element_size, size_t expr_id_offset,
    ngx_expr_eval_pt eval)
{
    u_char                     *p;
    ngx_uint_t                 i;
    ngx_expr_when_id_t         *expr_id;

    if (values == NULL || values == NGX_CONF_UNSET_PTR) {
        return NULL;
    }

    p = values->elts;

    for (i = 0; i < values->nelts; i++, p += element_size) {
        expr_id = (ngx_expr_when_id_t *) (p + expr_id_offset);

        if (*expr_id == NGX_EXPR_NO_WHEN_ID
            || eval(data, *expr_id) == NGX_EXPR_WHEN_HIT)
        {
            return p;
        }
    }

    return NULL;
}


static ngx_uint_t
ngx_expr_array_has_values(ngx_array_t *values)
{
    return values != NULL && values != NGX_CONF_UNSET_PTR
           && values->nelts != 0;
}


static ngx_int_t
ngx_expr_append_array(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *source, size_t element_size)
{
    void          *p;
    ngx_array_t   *copy;

    if (!ngx_expr_array_has_values(source)) {
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
        *values = ngx_array_create(cf->pool, source->nelts + 1, element_size);
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
ngx_expr_append_default(ngx_conf_t *cf, ngx_array_t **values,
    size_t element_size, size_t value_offset, size_t value_size,
    size_t expr_id_offset, const void *default_value)
{
    u_char                    *ctx;
    ngx_expr_when_id_t        *expr_id;

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

    expr_id = (ngx_expr_when_id_t *) (ctx + expr_id_offset);
    *expr_id = NGX_EXPR_NO_WHEN_ID;

    return NGX_OK;
}


static ngx_int_t
ngx_expr_init_expr_array(ngx_conf_t *cf, ngx_array_t **values,
    size_t element_size, size_t value_offset, size_t value_size,
    size_t expr_id_offset, const void *default_value)
{
    if (ngx_expr_array_has_values(*values)) {
        if ((*values)->size != element_size) {
            return NGX_ERROR;
        }

        if (ngx_expr_find_ctx(*values, NGX_EXPR_NO_WHEN_ID,
                                        element_size, expr_id_offset)
            != NULL)
        {
            return NGX_OK;
        }
    }

    return ngx_expr_append_default(cf, values, element_size,
                                        value_offset, value_size,
                                        expr_id_offset, default_value);
}


static ngx_int_t
ngx_expr_merge_expr_array(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, size_t element_size, size_t value_offset,
    size_t value_size, size_t expr_id_offset, const void *default_value)
{
    if (!ngx_expr_array_has_values(*values)) {
        if (ngx_expr_array_has_values(prev)) {
            if (prev->size != element_size) {
                return NGX_ERROR;
            }

            if (ngx_expr_find_ctx(prev, NGX_EXPR_NO_WHEN_ID,
                                            element_size, expr_id_offset)
                != NULL)
            {
                *values = prev;
                return NGX_OK;
            }

            if (ngx_expr_append_array(cf, values, prev, element_size)
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        return ngx_expr_init_expr_array(cf, values,
                   element_size, value_offset, value_size, expr_id_offset,
                   default_value);
    }

    if ((*values)->size != element_size) {
        return NGX_ERROR;
    }

    if (ngx_expr_find_ctx(*values, NGX_EXPR_NO_WHEN_ID,
                                    element_size, expr_id_offset)
        != NULL)
    {
        return NGX_OK;
    }

    if (ngx_expr_append_array(cf, values, prev, element_size) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_expr_init_expr_array(cf, values, element_size,
               value_offset, value_size, expr_id_offset, default_value);
}


ngx_int_t
ngx_conf_init_expr_flag_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_flag_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_flag_ctx_t);
    value_offset = offsetof(ngx_conf_expr_flag_ctx_t, value);
    value_size = sizeof(ngx_flag_t);
    expr_id_offset = offsetof(ngx_conf_expr_flag_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_flag_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_flag_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_flag_ctx_t);
    value_offset = offsetof(ngx_conf_expr_flag_ctx_t, value);
    value_size = sizeof(ngx_flag_t);
    expr_id_offset = offsetof(ngx_conf_expr_flag_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_str_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_str_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_ctx_t);
    value_offset = offsetof(ngx_conf_expr_str_ctx_t, value);
    value_size = sizeof(ngx_str_t);
    expr_id_offset = offsetof(ngx_conf_expr_str_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_str_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_str_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_ctx_t);
    value_offset = offsetof(ngx_conf_expr_str_ctx_t, value);
    value_size = sizeof(ngx_str_t);
    expr_id_offset = offsetof(ngx_conf_expr_str_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_num_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_int_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_num_ctx_t);
    value_offset = offsetof(ngx_conf_expr_num_ctx_t, value);
    value_size = sizeof(ngx_int_t);
    expr_id_offset = offsetof(ngx_conf_expr_num_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_num_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_int_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_num_ctx_t);
    value_offset = offsetof(ngx_conf_expr_num_ctx_t, value);
    value_size = sizeof(ngx_int_t);
    expr_id_offset = offsetof(ngx_conf_expr_num_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_size_value(ngx_conf_t *cf, ngx_array_t **values,
    size_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_size_ctx_t);
    value_offset = offsetof(ngx_conf_expr_size_ctx_t, value);
    value_size = sizeof(size_t);
    expr_id_offset = offsetof(ngx_conf_expr_size_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_size_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, size_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_size_ctx_t);
    value_offset = offsetof(ngx_conf_expr_size_ctx_t, value);
    value_size = sizeof(size_t);
    expr_id_offset = offsetof(ngx_conf_expr_size_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_off_value(ngx_conf_t *cf, ngx_array_t **values,
    off_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_off_ctx_t);
    value_offset = offsetof(ngx_conf_expr_off_ctx_t, value);
    value_size = sizeof(off_t);
    expr_id_offset = offsetof(ngx_conf_expr_off_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_off_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, off_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_off_ctx_t);
    value_offset = offsetof(ngx_conf_expr_off_ctx_t, value);
    value_size = sizeof(off_t);
    expr_id_offset = offsetof(ngx_conf_expr_off_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_msec_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_msec_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_msec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_msec_ctx_t, value);
    value_size = sizeof(ngx_msec_t);
    expr_id_offset = offsetof(ngx_conf_expr_msec_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_msec_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_msec_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_msec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_msec_ctx_t, value);
    value_size = sizeof(ngx_msec_t);
    expr_id_offset = offsetof(ngx_conf_expr_msec_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_sec_value(ngx_conf_t *cf, ngx_array_t **values,
    time_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_sec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_sec_ctx_t, value);
    value_size = sizeof(time_t);
    expr_id_offset = offsetof(ngx_conf_expr_sec_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_sec_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, time_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_sec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_sec_ctx_t, value);
    value_size = sizeof(time_t);
    expr_id_offset = offsetof(ngx_conf_expr_sec_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_enum_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_uint_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_enum_ctx_t);
    value_offset = offsetof(ngx_conf_expr_enum_ctx_t, value);
    value_size = sizeof(ngx_uint_t);
    expr_id_offset = offsetof(ngx_conf_expr_enum_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_enum_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_uint_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_enum_ctx_t);
    value_offset = offsetof(ngx_conf_expr_enum_ctx_t, value);
    value_size = sizeof(ngx_uint_t);
    expr_id_offset = offsetof(ngx_conf_expr_enum_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_bitmask_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_uint_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bitmask_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, value);
    value_size = sizeof(ngx_uint_t);
    expr_id_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_bitmask_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_uint_t default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bitmask_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, value);
    value_size = sizeof(ngx_uint_t);
    expr_id_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


static size_t
ngx_expr_ptr_ctx_size(ngx_array_t *values, ngx_array_t *prev)
{
    if (values != NULL && values != NGX_CONF_UNSET_PTR) {
        return values->size;
    }

    if (prev != NULL && prev != NGX_CONF_UNSET_PTR) {
        return prev->size;
    }

    return sizeof(ngx_conf_expr_ptr_ctx_t);
}


ngx_int_t
ngx_conf_init_expr_ptr_value(ngx_conf_t *cf, ngx_array_t **values,
    void *default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = ngx_expr_ptr_ctx_size(*values, NULL);
    value_offset = offsetof(ngx_conf_expr_ptr_ctx_t, value);
    value_size = sizeof(void *);
    expr_id_offset = offsetof(ngx_conf_expr_ptr_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_ptr_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, void *default_value)
{
    size_t   element_size, value_offset, value_size, expr_id_offset;

    element_size = ngx_expr_ptr_ctx_size(*values, prev);
    value_offset = offsetof(ngx_conf_expr_ptr_ctx_t, value);
    value_size = sizeof(void *);
    expr_id_offset = offsetof(ngx_conf_expr_ptr_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


ngx_int_t
ngx_conf_init_expr_bufs_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_uint_t default_num, size_t default_size)
{
    ngx_bufs_t   default_value;
    size_t       element_size, value_offset, value_size, expr_id_offset;

    default_value.num = default_num;
    default_value.size = default_size;

    element_size = sizeof(ngx_conf_expr_bufs_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bufs_ctx_t, value);
    value_size = sizeof(ngx_bufs_t);
    expr_id_offset = offsetof(ngx_conf_expr_bufs_ctx_t, expr_id);

    return ngx_expr_init_expr_array(cf, values, element_size,
                                                value_offset, value_size,
                                                expr_id_offset, &default_value);
}


ngx_int_t
ngx_conf_merge_expr_bufs_value(ngx_conf_t *cf, ngx_array_t **values,
    ngx_array_t *prev, ngx_uint_t default_num, size_t default_size)
{
    ngx_bufs_t   default_value;
    size_t       element_size, value_offset, value_size, expr_id_offset;

    default_value.num = default_num;
    default_value.size = default_size;

    element_size = sizeof(ngx_conf_expr_bufs_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bufs_ctx_t, value);
    value_size = sizeof(ngx_bufs_t);
    expr_id_offset = offsetof(ngx_conf_expr_bufs_ctx_t, expr_id);

    return ngx_expr_merge_expr_array(cf, values, prev, element_size,
                                                 value_offset, value_size,
                                                 expr_id_offset,
                                                 &default_value);
}


static void *
ngx_expr_prepare_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    size_t element_size, size_t value_offset, size_t expr_id_offset,
    ngx_expr_init_value_pt init, ngx_uint_t *created)
{
    void                     *ctx;
    ngx_array_t              **array;
    ngx_expr_when_id_t       *expr_id;

    array = (ngx_array_t **) ((u_char *) conf + cmd->offset);

    if (*array == NULL || *array == NGX_CONF_UNSET_PTR) {
        *array = ngx_array_create(cf->pool, 2, element_size);
        if (*array == NULL) {
            return NULL;
        }
    }

    ctx = ngx_expr_find_ctx(*array,
              ngx_expr_current_when_id, element_size, expr_id_offset);
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

    expr_id = (ngx_expr_when_id_t *)
                  ((u_char *) ctx + expr_id_offset);
    *expr_id = ngx_expr_current_when_id;
    *created = 1;

    return ctx;
}


static char *
ngx_expr_call_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf,
    size_t element_size, size_t value_offset, size_t expr_id_offset,
    ngx_expr_init_value_pt init,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *))
{
    char            *rv;
    void            *ctx;
    ngx_uint_t      created;
    ngx_array_t     **array;
    ngx_command_t   local_cmd;

    created = 0;
    ctx = ngx_expr_prepare_slot(cf, cmd, conf, element_size,
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


static void
ngx_expr_init_flag(void *data, size_t value_offset)
{
    ngx_flag_t   *value;

    value = (ngx_flag_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET;
}


static void
ngx_expr_init_num(void *data, size_t value_offset)
{
    ngx_int_t   *value;

    value = (ngx_int_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET;
}


static void
ngx_expr_init_size(void *data, size_t value_offset)
{
    size_t   *value;

    value = (size_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET_SIZE;
}


static void
ngx_expr_init_off(void *data, size_t value_offset)
{
    off_t   *value;

    value = (off_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET;
}


static void
ngx_expr_init_msec(void *data, size_t value_offset)
{
    ngx_msec_t   *value;

    value = (ngx_msec_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET_MSEC;
}


static void
ngx_expr_init_sec(void *data, size_t value_offset)
{
    time_t   *value;

    value = (time_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET;
}


static void
ngx_expr_init_enum(void *data, size_t value_offset)
{
    ngx_uint_t   *value;

    value = (ngx_uint_t *) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET_UINT;
}


static void
ngx_expr_init_bitmask(void *data, size_t value_offset)
{
    ngx_uint_t   *value;

    value = (ngx_uint_t *) ((u_char *) data + value_offset);
    *value = 0;
}


static void
ngx_expr_init_str(void *data, size_t value_offset)
{
    ngx_str_t   *value;

    value = (ngx_str_t *) ((u_char *) data + value_offset);
    value->len = 0;
    value->data = NULL;
}


static void
ngx_expr_init_ptr(void *data, size_t value_offset)
{
    void   **value;

    value = (void **) ((u_char *) data + value_offset);
    *value = NGX_CONF_UNSET_PTR;
}


static void
ngx_expr_init_bufs(void *data, size_t value_offset)
{
    ngx_bufs_t   *value;

    value = (ngx_bufs_t *) ((u_char *) data + value_offset);
    ngx_memzero(value, sizeof(ngx_bufs_t));
}


char *
ngx_expr_call_ptr_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf, size_t element_size, size_t value_offset,
    size_t expr_id_offset,
    char *(*setter)(ngx_conf_t *, ngx_command_t *, void *))
{
    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_ptr,
                                   setter);
}


char *
ngx_conf_set_conditional_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_flag_ctx_t);
    value_offset = offsetof(ngx_conf_expr_flag_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_flag_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_flag,
                                   ngx_conf_set_flag_slot);
}


char *
ngx_conf_set_conditional_str_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_ctx_t);
    value_offset = offsetof(ngx_conf_expr_str_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_str_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_str,
                                   ngx_conf_set_str_slot);
}


char *
ngx_conf_set_conditional_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_str_array_ctx_t);
    value_offset = offsetof(ngx_conf_expr_str_array_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_str_array_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_ptr,
                                   ngx_conf_set_str_array_slot);
}


char *
ngx_conf_set_conditional_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_keyval_ctx_t);
    value_offset = offsetof(ngx_conf_expr_keyval_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_keyval_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_ptr,
                                   ngx_conf_set_keyval_slot);
}


char *
ngx_conf_set_conditional_num_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_num_ctx_t);
    value_offset = offsetof(ngx_conf_expr_num_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_num_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_num,
                                   ngx_conf_set_num_slot);
}


char *
ngx_conf_set_conditional_size_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_size_ctx_t);
    value_offset = offsetof(ngx_conf_expr_size_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_size_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_size,
                                   ngx_conf_set_size_slot);
}


char *
ngx_conf_set_conditional_off_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_off_ctx_t);
    value_offset = offsetof(ngx_conf_expr_off_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_off_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_off,
                                   ngx_conf_set_off_slot);
}


char *
ngx_conf_set_conditional_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_msec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_msec_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_msec_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_msec,
                                   ngx_conf_set_msec_slot);
}


char *
ngx_conf_set_conditional_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_sec_ctx_t);
    value_offset = offsetof(ngx_conf_expr_sec_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_sec_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_sec,
                                   ngx_conf_set_sec_slot);
}


char *
ngx_conf_set_conditional_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bufs_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bufs_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_bufs_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_bufs,
                                   ngx_conf_set_bufs_slot);
}


char *
ngx_conf_set_conditional_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_enum_ctx_t);
    value_offset = offsetof(ngx_conf_expr_enum_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_enum_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_enum,
                                   ngx_conf_set_enum_slot);
}


char *
ngx_conf_set_conditional_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    size_t   element_size, value_offset, expr_id_offset;

    element_size = sizeof(ngx_conf_expr_bitmask_ctx_t);
    value_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, value);
    expr_id_offset = offsetof(ngx_conf_expr_bitmask_ctx_t, expr_id);

    return ngx_expr_call_slot(cf, cmd, conf, element_size, value_offset,
                                   expr_id_offset, ngx_expr_init_bitmask,
                                   ngx_conf_set_bitmask_slot);
}
