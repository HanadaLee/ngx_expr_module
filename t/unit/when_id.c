
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include "ngx_expr.h"


#define check(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                               \
        }                                                                     \
    } while (0)


/* Only logging is stubbed; allocation, parsing and interning are real code. */

void
ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...)
{
    (void) level;
    (void) log;
    (void) err;

    fprintf(stderr, "unexpected allocation log: %s\n", fmt);
    exit(EXIT_FAILURE);
}


void ngx_cdecl
ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_err_t err,
    const char *fmt, ...)
{
    (void) level;
    (void) cf;
    (void) err;

    fprintf(stderr, "unexpected configuration error: %s\n", fmt);
    exit(EXIT_FAILURE);
}


static void
test_fields(ngx_conf_t *cf)
{
    ngx_uint_t           fill, i, count;
    ngx_array_t          terms;
    ngx_expr_term_t      *term;
    ngx_expr_when_id_t    original, id;
    ngx_expr_registry_t   registry;

    check(ngx_expr_registry_init(cf->pool, &registry) == NGX_OK);
    check(ngx_array_init(&terms, cf->pool, 3,
                         sizeof(ngx_expr_term_t)) == NGX_OK);

    term = terms.elts;
    terms.nelts = 2;
    original = NGX_EXPR_NO_WHEN_ID;

    /* Vary all padding and unused bit-field bits, not the logical fields. */

    for (fill = 0; fill < 256; fill++) {
        ngx_memset(term, fill, 3 * sizeof(ngx_expr_term_t));
        term[0].expr_id = 1;
        term[0].negative = 0;
        term[1].expr_id = 2;
        term[1].negative = 1;

        id = ngx_expr_get_when_id(cf, &registry, &terms);
        check(id != NGX_EXPR_NO_WHEN_ID);

        if (fill == 0) {
            original = id;
        }

        check(id == original);
        check(registry.expressions.nelts == 1);
    }

    /* Each semantic difference must register a distinct condition. */

    term[0].expr_id = 3;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 1);
    term[0].expr_id = 1;
    term[0].negative = 1;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 2);
    term[0].negative = 0;
    term[1].negative = 0;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 3);

    term[0].expr_id = 2;
    term[0].negative = 1;
    term[1].expr_id = 1;
    term[1].negative = 0;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 4);

    term[0].expr_id = 1;
    term[0].negative = 0;
    term[1].expr_id = 2;
    term[1].negative = 1;
    terms.nelts = 1;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 5);

    terms.nelts = 3;
    term[2].expr_id = 1;
    term[2].negative = 0;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == 6);

    /* Registry growth and mutation of the input cannot change stored terms. */

    terms.nelts = 2;
    for (i = 100; i < 200; i++) {
        term[0].expr_id = i;
        count = registry.expressions.nelts;
        check(ngx_expr_get_when_id(cf, &registry, &terms) == count);
        check(registry.expressions.nelts == count + 1);
    }

    term[0].expr_id = 1;
    count = registry.expressions.nelts;
    check(ngx_expr_get_when_id(cf, &registry, &terms) == original);
    check(registry.expressions.nelts == count);
}


static void
test_parser(ngx_conf_t *cf)
{
    ngx_uint_t           fill;
    ngx_array_t          args, terms;
    ngx_expr_term_t      *term;
    ngx_expr_registry_t   registry;
    ngx_str_t            values[] = {
        ngx_string("when"), ngx_string("alpha"), ngx_string("!beta")
    };

    check(ngx_expr_registry_init(cf->pool, &registry) == NGX_OK);

    ngx_memzero(&args, sizeof(ngx_array_t));
    args.elts = values;
    args.nelts = sizeof(values) / sizeof(ngx_str_t);
    cf->args = &args;

    for (fill = 0; fill < 256; fill++) {
        check(ngx_array_init(&terms, cf->pool, 2,
                             sizeof(ngx_expr_term_t)) == NGX_OK);
        ngx_memset(terms.elts, fill, 2 * sizeof(ngx_expr_term_t));

        check(ngx_expr_parse_terms(cf, &registry, 1, &terms) == NGX_OK);
        check(terms.nelts == 2);

        term = terms.elts;
        check(term[0].expr_id == 0 && term[0].negative == 0);
        check(term[1].expr_id == 1 && term[1].negative == 1);
        check(ngx_expr_get_when_id(cf, &registry, &terms) == 0);
        check(registry.expressions.nelts == 1);
        check(registry.names.nelts == 2);
    }

    cf->args = NULL;
}


int
main(void)
{
    ngx_log_t   log;
    ngx_conf_t  cf;

    ngx_memzero(&log, sizeof(ngx_log_t));
    log.log_level = NGX_LOG_EMERG;
    ngx_pagesize = getpagesize();

    ngx_memzero(&cf, sizeof(ngx_conf_t));
    cf.pool = ngx_create_pool(16384, &log);
    check(cf.pool != NULL);

    test_fields(&cf);
    test_parser(&cf);

    ngx_destroy_pool(cf.pool);
    puts("when ID: padding, fields, order, length, growth and parsing passed");

    return EXIT_SUCCESS;
}
