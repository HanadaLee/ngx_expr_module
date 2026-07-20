# ngx_condition_module

`ngx_condition_module` adds reusable, named conditions and conditional
configuration blocks to NGINX HTTP and Stream modules. A condition is defined
once with `condition` and can then be referenced by `when`, by another logical
condition, or by a condition-aware module through the public C API.

The addon builds two protocol modules from one source tree:

- `ngx_http_condition_module` for `http`, `server`, and `location` contexts.
- `ngx_stream_condition_module` for `stream` and Stream `server` contexts.

The module evaluates expressions against the current request or session. It
does not cache results, so a repeated evaluation can observe variables that
changed between processing phases.

## Table of contents

- [ngx\_condition\_module](#ngx_condition_module)
  - [Table of contents](#table-of-contents)
  - [Status](#status)
  - [Features](#features)
  - [Synopsis](#synopsis)
  - [Installation](#installation)
    - [Requirements](#requirements)
    - [Optional cJSON support](#optional-cjson-support)
    - [Static build](#static-build)
  - [Configuration](#configuration)
    - [`condition`](#condition)
    - [`when`](#when)
    - [Condition operators](#condition-operators)
      - [Logic](#logic)
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
  - [Author](#author)
  - [License](#license)

## Status

This module is experimental. Its configuration syntax and public C API should
be treated as evolving interfaces.

## Features

- Named conditions shared by configuration directives.
- HTTP and Stream implementations with the same syntax and behavior.
- Forward references, including references across configuration levels.
- Per-reference negation and implicit AND in `when` blocks.
- Logical, string, numeric, time, IP/CIDR, regular-expression, and optional
  JSON predicates.
- Same-scope repeated definitions combined with implicit OR.
- Condition-aware replacements for common NGINX slot setters and complex-value
  setters.
- Typed request/session getters for selecting the first matching configured
  value.
- Configuration-time validation of undefined names, conflicting definitions,
  invalid arguments, and cyclic logical references.
- Dense ID-indexed runtime lookup without string searches or per-evaluation
  allocation.

## Synopsis

The directive inside `when` must explicitly opt in to condition support. The
following example assumes that `add_header` has been integrated as described in
[Integrating another module](#integrating-another-module):

```nginx
http {
    condition is_get str_eq -i $request_method GET;
    condition is_public str_starts_with $uri /public/;
    condition is_internal ip_range $remote_addr
        10.0.0.0/8 192.168.0.0/16;

    condition public_get and is_get is_public;
    condition permitted or public_get is_internal;

    server {
        listen 8080;

        when permitted !maintenance {
            add_header X-Access permitted always;
        }

        # Forward references are valid.
        condition maintenance str_eq $arg_maintenance 1;
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
- The cJSON development library only when `is_json` is required.

The addon may register its HTTP module, its Stream module, or both, depending
on which subsystems are enabled in the NGINX build. Enable Stream explicitly
with `--with-stream` when it is needed.

### Optional cJSON support

The `config` script probes the system cJSON library and defines `NGX_CJSON`
when the header, library, and length-aware parsing API are available. If the
probe fails, the rest of the module still builds, but `is_json` is not
registered as a condition type.

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
    --add-module=/path/to/ngx_condition_module \
    --with-stream

make -j"$(nproc)"
sudo make install
```

Omit `--with-stream` when only HTTP support is needed. Preserve any other
options required by your NGINX build.

`--add-dynamic-module` is intentionally rejected. The addon defines the global
`NGX_CONDITION` build macro and condition-enabled built-in or third-party
modules may link directly to its public API. All participating translation
units therefore need to be compiled together with the same macro value.

Confirm the resulting build and configuration with:

```bash
nginx -V
nginx -t
```

## Configuration

### `condition`

**Syntax:** `condition name operator arguments...;`

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

**Syntax:** `when condition_ref [condition_ref ...] { ... }`

**Default:** none

**HTTP contexts:** `http`, `server`, `location`

**Stream contexts:** `stream`, `server`

Associates every condition-aware configuration item in the block with the
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

### Condition operators

#### Logic

```nginx
condition name not condition_name;
condition name and condition_name1 condition_name2...;
condition name or condition_name1 condition_name2...;
```

- `not` negates one named condition.
- `and` accepts two or more names and short-circuits on the first non-match.
- `or` accepts two or more names and short-circuits on the first match.

Logical references may also be forward references. Cycles are rejected while
the effective configuration for each scope is finalized.

#### Empty values

```nginx
condition name is_empty complex_value;
condition name is_not_empty complex_value;
```

#### Strings

```nginx
condition name str_eq [-i] complex_value1 complex_value2;
condition name str_ne [-i] complex_value1 complex_value2;
condition name str_starts_with [-i] complex_value complex_value_prefix;
condition name str_ends_with [-i] complex_value complex_value_suffix;
condition name str_contains [-i] complex_value complex_value_substring;
condition name str_regex_match [-i] complex_value regex;
```

`-i` enables case-insensitive comparison for the operator that follows it. The
regular expression is compiled while loading the configuration and
requires NGINX PCRE support; it is not a complex value.

#### Numbers

```nginx
condition name is_num complex_value;
condition name num_eq complex_value1 complex_value2;
condition name num_ne complex_value1 complex_value2;
condition name num_lt complex_value1 complex_value2;
condition name num_le complex_value1 complex_value2;
condition name num_gt complex_value1 complex_value2;
condition name num_ge complex_value1 complex_value2;
condition name num_range complex_value complex_value_end;
condition name num_range complex_value complex_value_start complex_value_end;
```

`num_range value end` tests the closed interval `[0, end]`.
`num_range value start end` tests `[start, end]`. Decimal comparison is exact
and does not convert values to floating point, avoiding overflow and rounding
errors.

#### Time

```nginx
condition name time_range \
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
condition name is_ip complex_value;
condition name is_cidr complex_value;
condition name ip_range complex_value item...;
```

An `ip_range` item is written directly, without a leading keyword. Supported
items are a single IPv4 address, a single IPv6 address when NGINX IPv6 support
is enabled, an IPv4/IPv6 CIDR, or an inclusive IPv4 range:

```nginx
condition trusted ip_range $remote_addr
    127.0.0.1
    10.0.0.0/8
    2001:db8::/32
    192.0.2.10-192.0.2.20;
```

The items are alternatives and are evaluated with OR.

#### JSON

```nginx
condition name is_json complex_value;
```

`is_json` validates the complete value using cJSON and accepts any valid JSON
value, including objects, arrays, strings, numbers, booleans, and `null`. The
parse tree is released immediately after validation. This operator exists only
when the build probe defines `NGX_CJSON`.

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
   condition because the child definition replaces the parent definition.
6. A globally known name can have no effective definition in a particular
   scope. Its base result in that scope is a non-match; a `!name` term then
   negates that result.

For example:

```nginx
http {
    condition enabled str_eq $arg_mode yes;

    server {
        # The two local definitions replace the inherited definition and form
        # an implicit OR.
        condition enabled str_eq $arg_mode 1;
        condition enabled str_eq $arg_mode true;
    }
}
```

### Evaluation and priority

Expression results are not cached. Every call reevaluates the ordered terms
and their effective conditions using the current request or session values.

Condition-aware slot values are also kept in configuration order. The first
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

List-like directives may define different semantics. For example, an
integrated `add_header` can retain one expression ID per header and apply every
matching item instead of selecting only the first one.

## Integrating another module

The addon does not patch NGINX core. A built-in or third-party HTTP/Stream
module must explicitly opt in wherever one of its directives should work
inside `when`.

### Public headers and build guard

Use the protocol-specific public header only. Do not include the internal
`ngx_condition.h` directly.

```c
#if (NGX_CONDITION)
#include <ngx_http_condition_module.h>
/* or, in a Stream module: #include <ngx_stream_condition_module.h> */
#endif
```

The addon's `config` script defines `NGX_CONDITION` through NGINX's generated
configuration header. Like NGINX's own feature macros, an undefined
`NGX_CONDITION` evaluates to zero in `#if`. When the addon is absent, every
integration point must compile back to its original field layout, directive
flags, setter, merge logic, and runtime access path. Do not expose
condition-specific symbols from an unguarded branch.

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
#if (NGX_CONDITION)
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
ngx_uint_t ngx_http_condition_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_http_condition_from_when_cmd_type(ngx_uint_t type);

ngx_uint_t ngx_stream_condition_to_when_cmd_type(ngx_uint_t type);
ngx_uint_t ngx_stream_condition_from_when_cmd_type(ngx_uint_t type);
```

### Custom configuration handlers

For a custom item rather than a standard slot, store its associated expression
ID with the item itself:

```c
typedef struct {
    ngx_str_t                 value;
#if (NGX_CONDITION)
    ngx_condition_expr_id_t   expr_id;
#endif
} ngx_http_example_item_t;

/* In the directive parser, after allocating the item: */
#if (NGX_CONDITION)
item->expr_id = ngx_condition_get_associated_expr_id(cf);
#endif
```

An item parsed outside `when` receives `NGX_CONDITION_NO_EXPR_ID`, which means
unconditional. IDs are unsigned array indexes allocated during one NGINX
configuration cycle. Zero is valid; IDs are not stable across reloads and must
not be serialized or exposed as persistent configuration.

### Conditional slot setters and getters

A condition-aware standard slot changes from a scalar to `ngx_array_t *` while
`NGX_CONDITION` is enabled. Every element contains the original parsed value
and its own `expr_id`:

```c
typedef struct {
#if (NGX_CONDITION)
    ngx_array_t  *enabled; /* ngx_conf_condition_flag_ctx_t */
#else
    ngx_flag_t    enabled;
#endif
} ngx_http_example_loc_conf_t;
```

Initialize the array field to `NGX_CONF_UNSET_PTR` in `create_*_conf`. Its merge
logic must preserve the final element order and append any inherited/default
unconditional fallback after explicit entries. It must not reorder entries by
expression ID or by whether they are conditional.

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
ngx_http_set_conditional_complex_value_slot
ngx_http_set_conditional_complex_value_zero_slot
ngx_http_set_conditional_complex_value_size_slot
```

When `NGX_RESTY_EXT` is enabled, HTTP also exports:

```c
ngx_http_set_conditional_complex_value_msec_slot
ngx_http_set_conditional_complex_value_sec_slot
```

Stream complex-value replacements are:

```c
ngx_stream_set_conditional_complex_value_slot
ngx_stream_set_conditional_complex_value_zero_slot
ngx_stream_set_conditional_complex_value_size_slot
```

The shared array element types are `ngx_conf_condition_flag_ctx_t`,
`ngx_conf_condition_str_ctx_t`, `ngx_conf_condition_str_array_ctx_t`,
`ngx_conf_condition_keyval_ctx_t`, `ngx_conf_condition_num_ctx_t`,
`ngx_conf_condition_size_ctx_t`, `ngx_conf_condition_off_ctx_t`,
`ngx_conf_condition_msec_ctx_t`, `ngx_conf_condition_sec_ctx_t`,
`ngx_conf_condition_bufs_ctx_t`, `ngx_conf_condition_enum_ctx_t`, and
`ngx_conf_condition_bitmask_ctx_t`. HTTP and Stream complex values use
`ngx_http_condition_complex_value_ctx_t` and
`ngx_stream_condition_complex_value_ctx_t`, respectively.

At runtime, use the protocol-specific typed getter rather than implementing an
evaluation loop in each consumer:

| Value | HTTP getter | Stream getter |
| --- | --- | --- |
| flag | `ngx_http_get_conditional_flag_value` | `ngx_stream_get_conditional_flag_value` |
| string | `ngx_http_get_conditional_str_value` | `ngx_stream_get_conditional_str_value` |
| string array | `ngx_http_get_conditional_str_array_value` | `ngx_stream_get_conditional_str_array_value` |
| key/value array | `ngx_http_get_conditional_keyval_value` | `ngx_stream_get_conditional_keyval_value` |
| integer | `ngx_http_get_conditional_num_value` | `ngx_stream_get_conditional_num_value` |
| size | `ngx_http_get_conditional_size_value` | `ngx_stream_get_conditional_size_value` |
| offset | `ngx_http_get_conditional_off_value` | `ngx_stream_get_conditional_off_value` |
| milliseconds | `ngx_http_get_conditional_msec_value` | `ngx_stream_get_conditional_msec_value` |
| seconds | `ngx_http_get_conditional_sec_value` | `ngx_stream_get_conditional_sec_value` |
| buffers | `ngx_http_get_conditional_bufs_value` | `ngx_stream_get_conditional_bufs_value` |
| enum | `ngx_http_get_conditional_enum_value` | `ngx_stream_get_conditional_enum_value` |
| bitmask | `ngx_http_get_conditional_bitmask_value` | `ngx_stream_get_conditional_bitmask_value` |

For example:

```c
conf = ngx_http_get_module_loc_conf(r, ngx_http_example_module);

#if (NGX_CONDITION)
enabled = ngx_http_get_conditional_flag_value(r, conf->enabled);
#else
enabled = conf->enabled;
#endif
```

Complex-value getters are exported as full functions:

```c
ngx_int_t ngx_http_get_conditional_complex_value(
    ngx_http_request_t *r, ngx_array_t *values, ngx_str_t *value);
size_t ngx_http_get_conditional_complex_value_size(
    ngx_http_request_t *r, ngx_array_t *values, size_t default_value);

ngx_int_t ngx_stream_get_conditional_complex_value(
    ngx_stream_session_t *s, ngx_array_t *values, ngx_str_t *value);
size_t ngx_stream_get_conditional_complex_value_size(
    ngx_stream_session_t *s, ngx_array_t *values, size_t default_value);
```

The plain complex-value getter returns `NGX_DECLINED` when no entry applies.
Size getters return the caller's `default_value`. Under `NGX_RESTY_EXT`, HTTP
also provides `ngx_http_get_conditional_complex_value_msec` and
`ngx_http_get_conditional_complex_value_sec`.

### Expression result API

Consumers with custom configuration items can evaluate a saved expression ID
directly:

```c
#define NGX_CONDITION_EXPR_MISS  0
#define NGX_CONDITION_EXPR_HIT   1

ngx_int_t ngx_http_condition_get_expr_result(
    ngx_http_request_t *r, ngx_condition_expr_id_t expr_id);

ngx_int_t ngx_stream_condition_get_expr_result(
    ngx_stream_session_t *s, ngx_condition_expr_id_t expr_id);
```

Passing `NGX_CONDITION_NO_EXPR_ID` returns `NGX_CONDITION_EXPR_HIT`, preserving
the original unconditional behavior.

## Diagnostics and performance

Configuration parsing uses linear arrays for name and expression lookup because
it runs only while loading configuration. Finalized runtime structures are
dense arrays indexed by expression ID and condition ID, so runtime lookup does
not search by name.

Evaluation performs no per-request/session allocation. AND, OR, repeated
same-name definitions, and `when` term lists short-circuit in configuration
order. The total work is proportional to the expression nodes actually
visited. Results are deliberately not cached.

With NGINX debug logging enabled, the module writes expression IDs, term order,
condition IDs and names, negation, operator type, results, and short-circuit
points to the HTTP or Stream debug log. It does not emit per-request matching
details at normal log levels.

## Limitations

- Only static builds through `--add-module` are supported.
- Conditions and `when` are available only in HTTP and Stream configuration;
  there is no upstream context.
- `when` blocks cannot be nested.
- A directive is not condition-aware until its owning module explicitly opts
  in and evaluates the associated expression.
- `str_regex_match` requires NGINX PCRE support.
- `is_json` is omitted when the system cJSON development library is unavailable.
- Expression and condition IDs are valid only for one configuration cycle and
  can change after a reload.
- Expression results are intentionally not cached.

## Author

Hanada <im@hanada.info>

## License

This NGINX module is licensed under the [BSD 2-Clause License](LICENSE).
