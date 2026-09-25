#!/usr/bin/perl

# Tests for the optional ngx_expr_module JSON function.

###############################################################################

use warnings;
use strict;

use Test::More;

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http stream stream_return ngx_expr_module/);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format  expr_test  '$uri';

    expr h_object is_json '  {"valid":true}  ';
    expr h_array is_json '[1,2,3]';
    expr h_string is_json '"text"';
    expr h_number is_json '-1.25e+2';
    expr h_boolean is_json true;
    expr h_null is_json null;
    expr h_trailing !is_json '{"valid":true} trailing';
    expr h_truncated !is_json '{"valid":';
    expr h_empty !is_json '';

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        when h_object h_array h_string h_number h_boolean h_null
             h_trailing h_truncated h_empty {
            access_log %%TESTDIR%%/http-json-hit.log expr_test;
        }

        location / {
            return 204;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format  expr_test  '$remote_addr';

    expr s_object is_json '  {"valid":true}  ';
    expr s_array is_json '[1,2,3]';
    expr s_string is_json '"text"';
    expr s_number is_json '-1.25e+2';
    expr s_boolean is_json true;
    expr s_null is_json null;
    expr s_trailing !is_json '{"valid":true} trailing';
    expr s_truncated !is_json '{"valid":';
    expr s_empty !is_json '';

    server {
        listen  127.0.0.1:8081;

        when s_object s_array s_string s_number s_boolean s_null
             s_trailing s_truncated s_empty {
            access_log %%TESTDIR%%/stream-json-hit.log expr_test;
        }

        return ok;
    }
}

EOF

my $output = $t->dump_config();
my $status = $?;

plan(skip_all => 'is_json support is not available')
    if $status != 0 && $output =~ /unsupported expr type "is_json"/;

BAIL_OUT("failed to validate JSON test configuration:\n$output")
    if $status != 0;

$t->plan(4)->run()->waitforsocket('127.0.0.1:' . port(8080));

###############################################################################

like(http_get('/'), qr/^HTTP\/1\.1 204/, 'HTTP is_json request');
is(stream('127.0.0.1:' . port(8081))->read(), 'ok',
    'Stream is_json request');

$t->stop();

is($t->read_file('http-json-hit.log'), "/\n",
    'HTTP is_json and negation match');
is($t->read_file('stream-json-hit.log'), "127.0.0.1\n",
    'Stream is_json and negation match');

###############################################################################
