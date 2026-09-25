# ngx_expr_module

`ngx_expr_module` adds reusable, named expressions and conditional
configuration blocks to NGINX HTTP and Stream modules. An expression is defined
once with `expr` and can then be referenced by `when`, by another logical
expression, or by an expression-aware module through the public C API.

The addon builds two protocol modules from one source tree:

- `ngx_http_expr_module` for `http`, `server`, and `location` contexts.
- `ngx_stream_expr_module` for `stream` and Stream `server` contexts.

The module evaluates expressions against the current request or session. It
does not cache results, so a repeated evaluation can observe variables that
changed between processing phases.

## Table of contents

- [ngx\_expr\_module](#ngx_expr_module)
  - [Table of contents](#table-of-contents)
  - [Status](#status)
  - [Features](#features)
  - [Synopsis](#synopsis)
  - [Installation](#installation)
    - [Requirements](#requirements)
    - [Optional cJSON support](#optional-cjson-support)
    - [Static build](#static-build)
  - [Configuration](#configuration)
    - [`expr`](#expr)
    - [`when`](#when)
    - [Expression operators](#expression-operators)
      - [Logic](#logic)
      - [Boolean constants](#boolean-constants)
      - [Empty values](#empty-values)
      - [Strings](#strings)
      - [Numbers](#numbers)
      - [Time](#time)
      - [IP addresses and networks](#ip-addresses-and-networks)
      - [JSON](#json)
    - [Scope, inheritance, and repeated definitions](#scope-inheritance-and-repeated-definitions)
    - [Evaluation and priority](#evaluation-and-priority)
  - [Integrating another module](#integrating-another-module)
    - [Public headers and build guard](#public-headers-and-build-guard)
    - [Allowing a directive inside `when`](#allowing-a-directive-inside-when)
    - [Custom configuration handlers](#custom-configuration-handlers)
    - [Conditional slot setters and getters](#conditional-slot-setters-and-getters)
    - [Expression result API](#expression-result-api)
  - [Diagnostics and performance](#diagnostics-and-performance)
  - [Limitations](#limitations)
  - [Testing](#testing)
  - [Author](#author)
  - [License](#license)

## Status

This module is experimental. Its configuration syntax and public C API should
be treated as evolving interfaces.

Note: NGINX plans to introduce its own `condition` directive. This addon uses
`expr` and the `ngx_expr_module` name to avoid a directive name conflict.
The former `condition` directive is now `expr`. `when` directive is unchanged.
The `ngx_condition_module` addon, `ngx_http_condition_module` and
`ngx_stream_condition_module` are now named `ngx_expr_module`,
`ngx_http_expr_module` and `ngx_stream_expr_module`. External integrations must
switch from `NGX_CONDITION` to `NGX_EXPR`, include the new public headers, and
use the renamed types and functions. Only the `ngx_conf_set_conditional_*`
setter family retains its original names. The old `condition` directive and
old C symbols are no longer registered.

## Features

- Named expressions shared by configuration directives.
- HTTP and Stream implementations with the same syntax and behavior.
- Forward references, including references across configuration levels.
- Per-reference negation and implicit AND in `when` blocks.
- Boolean constants, logical, string, numeric, time, IP/CIDR,
  regular-expression, and optional JSON predicates.
- Same-scope repeated definitions combined with implicit OR.
- Expression-aware replacements for common NGINX slot setters, merge/default
  helpers, and complex-value setters.
- Typed request/session getters for selecting the first matching configured
  value.
- Configuration-time validation of undefined names, conflicting definitions,
  invalid arguments, and cyclic logical references.
- Dense ID-indexed runtime lookup without string searches or per-evaluation
  allocation.

## Synopsis

The directive inside `when` must explicitly opt in to expression support. The
following example assumes that `error_page` has been integrated as described in
[Integrating another module](#integrating-another-module):

```nginx
http {
    expr is_get str_eq -i $request_method GET;
    expr is_public str_starts_with $uri /public/;
    expr is_internal ip_range $remote_addr
        10.0.0.0/8 192.168.0.0/16;

    expr public_get and is_get is_public;
    expr permitted or public_get is_internal;

    server {
        listen 8080;

        when permitted !maintenance {
            error_page 418 =200 /handled;
        }

        location = /public/example {
            return 418;
        }

        location = /handled {
            internal;
            return 200 'handled';
        }

        # Forward references are valid.
        expr maintenance str_eq $arg_maintenance 1;
    }
}
```

The names after `when` are evaluated from left to right with implicit AND.
Prefixing a name with `!` negates only that term.

## Installation

### Requirements

- An NGINX source tree.
- A C compiler and the normal NGINX build dependencies.
- PCRE support in the NGINX build when `str_regex_match` is used.
- The cJSON development library only when `is_json` is required with nginx
  versions earlier than 1.31.5.

The addon may register its HTTP module, its Stream module, or both, depending
on which subsystems are enabled in the NGINX build. Enable Stream explicitly
with `--with-stream` when it is needed.

### Optional cJSON support

With nginx 1.31.5 and later, `is_json` uses the nginx core JSON parser and does
not require cJSON. With earlier nginx versions, the `config` script probes the
system cJSON library and defines `NGX_CJSON` when the header, library, and
length-aware parsing API are available. If the probe fails, the rest of the
module still builds, but `is_json` is not registered as an expression operator.

Common packages are:

```bash
# Debian/Ubuntu
sudo apt-get install libcjson-dev

# CentOS/RHEL
sudo yum install cjson-devel

# macOS
brew install cjson
```

cJSON is an external dependency and is not bundled in this repository.

### Static build

The current implementation must be linked statically into NGINX:

```bash
cd /path/to/nginx

./configure \
    --add-module=/path/to/ngx_expr_module \
    --with-stream

make -j"$(nproc)"
sudo make install
```

Omit `--with-stream` when only HTTP support is needed. Preserve any other
options required by your NGINX build.

`--add-dynamic-module` is intentionally rejected. The addon defines the global
`NGX_EXPR` build macro and expression-enabled built-in or third-party
modules may link directly to its public API. All participating translation
units therefore need to be compiled together with the same macro value.

Confirm the resulting build and configuration with:

```bash
nginx -V
nginx -t
```

## Configuration

### `expr`

**Syntax:** `expr name operator arguments...;`

**Default:** none

**HTTP contexts:** `http`, `server`, `location`

**Stream contexts:** `stream`, `server`

Defines a named predicate. A name may be referenced before or after its
definition and may be referenced from a different configuration level. Names
are case-sensitive, may not begin with `!`, and may contain at most 288 bytes.

Arguments described as `complex_value` are evaluated at request or session
time and may contain NGINX variables. A failed complex-value evaluation or an
invalid runtime conversion is a non-match.

### `when`

**Syntax:** `when expr_ref [expr_ref ...] { ... }`

**Default:** none

**HTTP contexts:** `http`, `server`, `location`

**Stream contexts:** `stream`, `server`

Associates every expression-aware configuration item in the block with the
ordered reference list. References are combined with implicit AND and
short-circuit at the first non-match:

```nginx
when authenticated !blocked from_internal_network {
    example_enabled on;
}
```

`!blocked` negates `blocked`. The `!` prefix and name must be one token.
`when` blocks cannot be nested.

`when` does not automatically make arbitrary NGINX directives conditional.
Each directive must declare the matching `when` context flag, retain the
expression ID while parsing, and evaluate it at runtime. Unsupported directives
are rejected by the normal NGINX configuration-context check.

### Expression operators

Every non-logical English operator can be negated by prefixing the operator
with one `!`, or by placing `not` before it:

```nginx
expr has_value !is_empty $arg_value;
expr is_external !str_starts_with $uri /internal/;
expr is_other_port not num_in $server_port 80 443;
```

The logical form `expr name not expr_ref;` is also preserved. When
the token after `not` is a non-logical operator name, `not` modifies that
operator; otherwise, it remains the logical operator over a named expression.

The following base symbolic aliases are registered:

| English operator | Symbol | Notes |
| --- | --- | --- |
| `str_eq` | `=` | String equality |
| `str_starts_with` | `^~` | String prefix |
| `str_ends_with` | `~$` | String suffix |
| `str_regex_match` | `~` | Regular expression |
| `str_regex_match -i` | `~*` | Case-insensitive regular expression |
| `num_eq` | `==` | Numeric equality |
| `num_lt` | `<` | Numeric less than |
| `num_le` | `<=` | Numeric less than or equal |
| `num_gt` | `>` | Numeric greater than |
| `num_ge` | `>=` | Numeric greater than or equal |

The same single `!` prefix applies to English and symbolic operators. Thus
`!=` and `!==` work naturally as `!` plus the registered `=` and `==` aliases;
they are not separate operator entries. Repeated prefixes such as `!!str_eq`,
`!!=`, and `!!==` are invalid. For example:

```nginx
expr not_internal !^~ $uri /internal/;
expr not_php !~$ $uri .php;
expr not_api !~* $uri ^/api/;
expr outside_range !>= $arg_score 60;
```

Negation is applied only after the underlying predicate has been evaluated
successfully. A failed complex-value evaluation, or a conversion failure in a
numeric, time, or IP comparison, remains a non-match. Validation predicates
behave normally under negation; for example, `!is_num` matches a value that
is not numeric.

#### Logic

```nginx
expr name not expr_ref;
expr name and expr_ref1 expr_ref2...;
expr name or expr_ref1 expr_ref2...;
```

- `not` negates one named expression.
- `and` accepts two or more names and short-circuits on the first non-match.
- `or` accepts two or more names and short-circuits on the first match.

Each `expr_ref` is either `expr_name` or `!expr_name`. The `!`
prefix negates only that reference and is supported by `not`, `and`, and `or`:

```nginx
expr allowed and authenticated !blocked;
expr fallback or primary !maintenance backup;
expr enabled not !configured;
```

Logical references may also be forward references. Cycles are rejected while
the effective configuration for each scope is finalized.

#### Boolean constants

```nginx
expr name bool true;
expr name bool false;
```

`bool` forces an expression to a constant result. Its value is case-sensitive
and must be exactly `true` or `false`.

#### Empty values

```nginx
expr name is_empty complex_value;
expr not_empty !is_empty complex_value;
```

#### Strings

```nginx
expr name str_eq [-i] complex_value1 complex_value2;
expr name str_starts_with [-i] complex_value complex_value_prefix;
expr name str_ends_with [-i] complex_value complex_value_suffix;
expr name str_contains [-i] complex_value complex_value_substring;
expr name str_regex_match [-i] complex_value regex;
expr name str_in [-i] complex_value candidate1 [candidate2 ...];
```

`-i` enables case-insensitive comparison for the operator that follows it. The
regular expression is compiled while loading the configuration and
requires NGINX PCRE support; it is not a complex value.

`str_in` compares the first value with each candidate in configuration order
and matches on the first equality. All candidates are complex values. It has
no symbolic alias. Use `!str_eq` or `!=` for string inequality.

The string symbols can be used directly in the same position:

```nginx
expr is_get = $request_method GET;
expr is_asset ^~ $uri /assets/;
expr is_javascript ~$ $uri .js;
expr is_api ~* $uri ^/API/;
```

#### Numbers

```nginx
expr name is_num complex_value;
expr name num_eq complex_value1 complex_value2;
expr name num_lt complex_value1 complex_value2;
expr name num_le complex_value1 complex_value2;
expr name num_gt complex_value1 complex_value2;
expr name num_ge complex_value1 complex_value2;
expr name num_range complex_value complex_value_end;
expr name num_range complex_value complex_value_start complex_value_end;
expr name num_in complex_value candidate1 [candidate2 ...];
```

`num_range value end` tests the closed interval `[0, end]`.
`num_range value start end` tests `[start, end]`. Decimal comparison is exact
and does not convert values to floating point, avoiding overflow and rounding
errors.

`num_in` performs exact numeric equality against each candidate in
configuration order and matches the first equal value. It has no symbolic
alias. Use `!num_eq` or `!==` for numeric inequality. Numeric symbols can be
used directly:

```nginx
expr is_success >= $status 200;
expr is_client_error < $status 500;
expr is_default_port num_in $server_port 80 443;
```

#### Time

```nginx
expr name time_range \
    [complex_value_current_timestamp] \
    [year=year_range] [month=month_range] [day=day_range] \
    [wday=wday_range] [hour=hour_range] [min=min_range] [sec=sec_range] \
    [gmt | gmt+HHMM | gmt-HHMM];
```

The optional timestamp is a Unix timestamp. When omitted, the current request
or session time is used. Fields are combined with AND and every range is
inclusive. A range is either one unsigned integer or `start-end`.

Accepted field bounds are:

| Field | Range |
| --- | --- |
| `year` | `1970-9999` |
| `month` | `1-12` |
| `day` | `1-31` |
| `wday` | `0-6`, where Sunday is `0` |
| `hour` | `0-23` |
| `min` | `0-59` |
| `sec` | `0-59` |

The default time zone is local time. `gmt` selects UTC; `gmt+HHMM` and
`gmt-HHMM` apply a fixed offset. A field or time zone may appear only once.

#### IP addresses and networks

```nginx
expr name is_ip complex_value;
expr name is_cidr complex_value;
expr name ip_range complex_value item...;
```

An `ip_range` item is written directly, without a leading keyword. Supported
items are a single IPv4 address, a single IPv6 address when NGINX IPv6 support
is enabled, an IPv4/IPv6 CIDR, or an inclusive IPv4 range:

```nginx
expr trusted ip_range $remote_addr
    127.0.0.1
    10.0.0.0/8
    2001:db8::/32
    192.0.2.10-192.0.2.20;
```

The items are alternatives and are evaluated with OR.

#### JSON

```nginx
expr name is_json complex_value;
```

`is_json` validates the complete value and accepts any valid JSON value,
including objects, arrays, strings, numbers, booleans, and `null`. It uses the
nginx core JSON parser with nginx 1.31.5 and later, and cJSON with earlier
versions.

### Scope, inheritance, and repeated definitions

HTTP and Stream maintain separate global name and expression registries. Within
one subsystem, these rules apply:

1. A name must be defined somewhere in the complete configuration. A name that
   is referenced but never defined causes configuration loading to fail.
2. If the current scope contains no local definition for a name, it inherits
   the effective definition from its parent scope.
3. One or more local definitions completely replace the inherited definition
   set for that name; parent and child definitions are not combined.
4. Repeated definitions of the same name in one scope must use the same exact
   operator. They are evaluated in configuration order with implicit OR.
5. A child scope may use a different operator from its parent's same-named
   expression because the child definition replaces the parent definition.
6. A globally known name can have no effective definition in a particular
   scope. Its base result in that scope is a non-match; a `!name` term then
   negates that result.

For example:

```nginx
http {
    expr enabled str_eq $arg_mode yes;

    server {
        # The two local definitions replace the inherited definition and form
        # an implicit OR.
        expr enabled str_eq $arg_mode 1;
        expr enabled str_eq $arg_mode true;
    }
}
```

### Evaluation and priority

Expression results are not cached. Every call reevaluates the ordered terms
and their effective expressions using the current request or session values.

Expression-aware slot values are also kept in configuration order. The first
unconditional item or matching conditional item wins. An unconditional item is
therefore not an automatically low-priority default:

```nginx
# This unconditional item wins before later items are examined.
example_enabled off;

when internal {
    example_enabled on;
}
```

Put conditional entries first when they should take precedence:

```nginx
when internal {
    example_enabled on;
}

example_enabled off;
```

List-like directives may define different semantics, such as applying every
matching item instead of selecting only the first one. Their integration must
define which behavior applies.

## Integrating another module

The addon does not patch NGINX core. A built-in or third-party HTTP/Stream
module must explicitly opt in wherever one of its directives should work
inside `when`.

### Public headers and build guard

Use the protocol-specific public header only. Do not include the internal
`ngx_expr.h` directly.

```c
#if (NGX_EXPR)
#include <ngx_http_expr_module.h>
/* or, in a Stream module: #include <ngx_stream_expr_module.h> */
#endif
```

The addon's `config` script defines `NGX_EXPR` through NGINX's generated
configuration header. Like NGINX's own feature macros, an undefined
`NGX_EXPR` evaluates to zero in `#if`. When the addon is absent, every
integration point must compile back to its original field layout, directive
flags, setter, merge logic, and runtime access path. Do not expose
expression-specific symbols from an unguarded branch.

### Allowing a directive inside `when`

The public context flags are:

| HTTP | Stream |
| --- | --- |
| `NGX_HTTP_MAIN_WHEN_CONF` | `NGX_STREAM_MAIN_WHEN_CONF` |
| `NGX_HTTP_SRV_WHEN_CONF` | `NGX_STREAM_SRV_WHEN_CONF` |
| `NGX_HTTP_LOC_WHEN_CONF` | — |

Add only the `when` flag corresponding to a context the directive already
supports. Do not use a `when` flag to broaden the directive's normal scope.

```c
#if (NGX_EXPR)
{ ngx_string("example_enabled"),
  NGX_HTTP_LOC_CONF | NGX_HTTP_LOC_WHEN_CONF | NGX_CONF_FLAG,
  ngx_conf_set_conditional_flag_slot,
  NGX_HTTP_LOC_CONF_OFFSET,
  offsetof(ngx_http_example_loc_conf_t, enabled),
  NULL },
#else
{ ngx_string("example_enabled"),
  NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
  ngx_conf_set_flag_slot,
  NGX_HTTP_LOC_CONF_OFFSET,
  offsetof(ngx_http_example_loc_conf_t, enabled),
  NULL },
#endif
```

The conversion helpers are available for custom configuration parsers:

```c
ngx_uint_t ngx_http_expr_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_http_expr_from_when_cmd_type(ngx_uint_t type);

ngx_uint_t ngx_stream_expr_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_stream_expr_from_when_cmd_type(ngx_uint_t type);
```

### Custom configuration handlers

For a custom item rather than a standard slot, store its associated expression
ID with the item itself:

```c
typedef struct {
    ngx_str_t           value;
#if (NGX_EXPR)
    ngx_expr_when_id_t  expr_id;
#endif
} ngx_http_example_item_t;

/* In the directive parser, after allocating the item: */
#if (NGX_EXPR)
item->expr_id = ngx_expr_get_associated_when_id(cf);
#endif
```

An item parsed outside `when` receives `NGX_EXPR_NO_WHEN_ID`, which means
unconditional. `ngx_expr_id_t` identifies a named expression, while
`ngx_expr_when_id_t` identifies the reference list attached to a `when` block.
Both are unsigned array indexes allocated during one NGINX configuration cycle.
Zero is valid; IDs are not stable across reloads and must not be serialized or
exposed as persistent configuration.

### Conditional slot setters and getters

An expression-aware standard slot changes from a scalar to `ngx_array_t *` while
`NGX_EXPR` is enabled. Every element contains the original parsed value
and its own `expr_id`:

```c
typedef struct {
#if (NGX_EXPR)
    ngx_array_t  *enabled; /* ngx_conf_expr_flag_ctx_t */
#else
    ngx_flag_t    enabled;
#endif
} ngx_http_example_loc_conf_t;
```

Initialize the array field to `NGX_CONF_UNSET_PTR` in `create_*_conf`. The
common init and merge helpers preserve element order and ensure that inherited
or default values remain lower-priority fallbacks:

- `init` keeps existing entries and appends an unconditional default only when
  the array has no unconditional entry.
- `merge` directly inherits the parent array when the child has no entries.
- If the child has conditional entries but no unconditional entry, the parent
  array is appended after the child entries.
- If neither array supplies an unconditional entry, the default is appended
  last.
- A child unconditional entry completes the local value set, so no parent
  entries are appended.

All helpers return `NGX_OK` or `NGX_ERROR`. The supported scalar and fixed-value
families are:

| Value | Init helper | Merge helper |
| --- | --- | --- |
| flag | `ngx_conf_init_expr_flag_value` | `ngx_conf_merge_expr_flag_value` |
| string | `ngx_conf_init_expr_str_value` | `ngx_conf_merge_expr_str_value` |
| pointer | `ngx_conf_init_expr_ptr_value` | `ngx_conf_merge_expr_ptr_value` |
| integer | `ngx_conf_init_expr_num_value` | `ngx_conf_merge_expr_num_value` |
| size | `ngx_conf_init_expr_size_value` | `ngx_conf_merge_expr_size_value` |
| offset | `ngx_conf_init_expr_off_value` | `ngx_conf_merge_expr_off_value` |
| milliseconds | `ngx_conf_init_expr_msec_value` | `ngx_conf_merge_expr_msec_value` |
| seconds | `ngx_conf_init_expr_sec_value` | `ngx_conf_merge_expr_sec_value` |
| buffers | `ngx_conf_init_expr_bufs_value` | `ngx_conf_merge_expr_bufs_value` |
| enum | `ngx_conf_init_expr_enum_value` | `ngx_conf_merge_expr_enum_value` |
| bitmask | `ngx_conf_init_expr_bitmask_value` | `ngx_conf_merge_expr_bitmask_value` |

For example:

```c
static char *
ngx_http_example_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_example_loc_conf_t  *prev = parent;
    ngx_http_example_loc_conf_t  *conf = child;

    if (ngx_conf_merge_expr_flag_value(cf, &conf->enabled,
                                              prev->enabled, 0)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
```

Generic pointers use `ngx_conf_expr_ptr_ctx_t`; HTTP and Stream consumers
can retrieve them with `ngx_http_get_expr_ptr_value` and
`ngx_stream_get_expr_ptr_value`. The pointer init/merge helpers derive
the element size from the child or parent array, so the value may be any object
pointer type, including the typed HTTP and Stream complex-value pointers. As
with NGINX's native pointer merge macro, the caller is responsible for passing
a compatible pointer-value context array. `ngx_conf_expr_str_array_ctx_t`
and `ngx_conf_expr_keyval_ctx_t` deliberately have no generic init/merge
helper because their values are arrays with module-specific replacement or
append semantics.

The common setter replacements are:

| Native setter | Conditional setter |
| --- | --- |
| `ngx_conf_set_flag_slot` | `ngx_conf_set_conditional_flag_slot` |
| `ngx_conf_set_str_slot` | `ngx_conf_set_conditional_str_slot` |
| `ngx_conf_set_str_array_slot` | `ngx_conf_set_conditional_str_array_slot` |
| `ngx_conf_set_keyval_slot` | `ngx_conf_set_conditional_keyval_slot` |
| `ngx_conf_set_num_slot` | `ngx_conf_set_conditional_num_slot` |
| `ngx_conf_set_size_slot` | `ngx_conf_set_conditional_size_slot` |
| `ngx_conf_set_off_slot` | `ngx_conf_set_conditional_off_slot` |
| `ngx_conf_set_msec_slot` | `ngx_conf_set_conditional_msec_slot` |
| `ngx_conf_set_sec_slot` | `ngx_conf_set_conditional_sec_slot` |
| `ngx_conf_set_bufs_slot` | `ngx_conf_set_conditional_bufs_slot` |
| `ngx_conf_set_enum_slot` | `ngx_conf_set_conditional_enum_slot` |
| `ngx_conf_set_bitmask_slot` | `ngx_conf_set_conditional_bitmask_slot` |

HTTP complex-value replacements are:

```c
ngx_http_set_expr_complex_value_slot
ngx_http_set_expr_complex_value_zero_slot
ngx_http_set_expr_complex_value_size_slot
```

When `NGX_RESTY_EXT` is enabled, HTTP also exports:

```c
ngx_http_set_expr_complex_value_msec_slot
ngx_http_set_expr_complex_value_sec_slot
```

Stream complex-value replacements are:

```c
ngx_stream_set_expr_complex_value_slot
ngx_stream_set_expr_complex_value_zero_slot
ngx_stream_set_expr_complex_value_size_slot
```

The shared array element types are `ngx_conf_expr_flag_ctx_t`,
`ngx_conf_expr_str_ctx_t`, `ngx_conf_expr_str_array_ctx_t`,
`ngx_conf_expr_keyval_ctx_t`, `ngx_conf_expr_num_ctx_t`,
`ngx_conf_expr_ptr_ctx_t`, `ngx_conf_expr_size_ctx_t`,
`ngx_conf_expr_off_ctx_t`, `ngx_conf_expr_msec_ctx_t`,
`ngx_conf_expr_sec_ctx_t`, `ngx_conf_expr_bufs_ctx_t`,
`ngx_conf_expr_enum_ctx_t`, and
`ngx_conf_expr_bitmask_ctx_t`. HTTP and Stream complex values use
`ngx_http_expr_complex_value_ctx_t` and
`ngx_stream_expr_complex_value_ctx_t`, respectively. They remain typed
for protocol-specific compilation and evaluation while sharing the generic
pointer init/merge implementation.

At runtime, use the protocol-specific typed getter rather than implementing an
evaluation loop in each consumer:

| Value | HTTP getter | Stream getter |
| --- | --- | --- |
| flag | `ngx_http_get_expr_flag_value` | `ngx_stream_get_expr_flag_value` |
| string | `ngx_http_get_expr_str_value` | `ngx_stream_get_expr_str_value` |
| pointer | `ngx_http_get_expr_ptr_value` | `ngx_stream_get_expr_ptr_value` |
| string array | `ngx_http_get_expr_str_array_value` | `ngx_stream_get_expr_str_array_value` |
| key/value array | `ngx_http_get_expr_keyval_value` | `ngx_stream_get_expr_keyval_value` |
| integer | `ngx_http_get_expr_num_value` | `ngx_stream_get_expr_num_value` |
| size | `ngx_http_get_expr_size_value` | `ngx_stream_get_expr_size_value` |
| offset | `ngx_http_get_expr_off_value` | `ngx_stream_get_expr_off_value` |
| milliseconds | `ngx_http_get_expr_msec_value` | `ngx_stream_get_expr_msec_value` |
| seconds | `ngx_http_get_expr_sec_value` | `ngx_stream_get_expr_sec_value` |
| buffers | `ngx_http_get_expr_bufs_value` | `ngx_stream_get_expr_bufs_value` |
| enum | `ngx_http_get_expr_enum_value` | `ngx_stream_get_expr_enum_value` |
| bitmask | `ngx_http_get_expr_bitmask_value` | `ngx_stream_get_expr_bitmask_value` |

For example:

```c
conf = ngx_http_get_module_loc_conf(r, ngx_http_example_module);

#if (NGX_EXPR)
enabled = ngx_http_get_expr_flag_value(r, conf->enabled);
#else
enabled = conf->enabled;
#endif
```

Complex-value getters are exported as full functions:

```c
ngx_int_t ngx_http_get_expr_complex_value(
    ngx_http_request_t *r, ngx_array_t *values, ngx_str_t *value);
size_t ngx_http_get_expr_complex_value_size(
    ngx_http_request_t *r, ngx_array_t *values, size_t default_value);

ngx_int_t ngx_stream_get_expr_complex_value(
    ngx_stream_session_t *s, ngx_array_t *values, ngx_str_t *value);
size_t ngx_stream_get_expr_complex_value_size(
    ngx_stream_session_t *s, ngx_array_t *values, size_t default_value);
```

The plain complex-value getter returns `NGX_DECLINED` when no entry applies.
Size getters return the caller's `default_value`. Under `NGX_RESTY_EXT`, HTTP
also provides `ngx_http_get_expr_complex_value_msec` and
`ngx_http_get_expr_complex_value_sec`.

### Expression result API

Consumers with custom configuration items can evaluate a saved `when` ID
directly:

```c
#define NGX_EXPR_WHEN_MISS  0
#define NGX_EXPR_WHEN_HIT   1

ngx_int_t ngx_http_expr_get_result(
    ngx_http_request_t *r, ngx_expr_when_id_t expr_id);

ngx_int_t ngx_stream_expr_get_result(
    ngx_stream_session_t *s, ngx_expr_when_id_t expr_id);
```

Passing `NGX_EXPR_NO_WHEN_ID` returns `NGX_EXPR_WHEN_HIT`, preserving
the original unconditional behavior.

## Diagnostics and performance

Configuration parsing uses linear arrays for name and `when` expression lookup
because it runs only while loading configuration. Finalized runtime structures
are dense arrays indexed by named expression ID and `when` expression ID, so
runtime lookup does not search by name.

Evaluation performs no per-request/session allocation. AND, OR, repeated
same-name definitions, and `when` term lists short-circuit in configuration
order. The total work is proportional to the expression nodes actually
visited. Results are deliberately not cached.

With NGINX debug logging enabled, the module writes `when` expression IDs, term
order, named expression IDs and names, negation, operator type, results, and
short-circuit points to the HTTP or Stream debug log. It does not emit per-request matching
details at normal log levels.

## Limitations

- Only static builds through `--add-module` are supported.
- Expressions and `when` are available only in HTTP and Stream configuration;
  there is no upstream context.
- `when` blocks cannot be nested.
- A directive is not expression-aware until its owning module explicitly opts
  in and evaluates the associated expression.
- `str_regex_match` requires NGINX PCRE support.
- With nginx versions earlier than 1.31.5, `is_json` is omitted when the system
  cJSON development library is unavailable.
- Named expressions and expression IDs are valid only for one configuration
  cycle and can change after a reload.
- Expression results are intentionally not cached.

## Testing

The test suite uses the official
[nginx-tests](https://github.com/nginx/nginx-tests) `Test::Nginx` framework.
First build NGINX with this module and the modules exercised by the tests:

```sh
cd /path/to/nginx-1.31.3
./configure \
    --with-stream \
    --with-debug \
    --with-http_sub_module \
    --add-module=/path/to/ngx_expr_module
make -j2
```

Then run the Perl tests from the module directory:

```sh
TEST_NGINX_BINARY=/path/to/nginx-1.31.3/objs/nginx \
    prove -I /path/to/nginx-tests/lib t
```

The suite covers HTTP and Stream operators, invalid configurations, scope
inheritance, and expression-aware built-in directives. The JSON test is skipped
when the build does not provide `is_json`. Set `TEST_NGINX_VERBOSE=1` for
verbose protocol logging or `TEST_NGINX_LEAVE=1` to retain temporary test
directories.

## Author

Hanada <im@hanada.info>

## License

This NGINX module is licensed under the [BSD 2-Clause License](LICENSE).
