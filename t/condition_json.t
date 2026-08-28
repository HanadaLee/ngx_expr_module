#!/usr/bin/perl

# Tests for the optional ngx_condition_module JSON function.

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
    ->has(qw/http stream stream_return ngx_condition_module/);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format  condition_test  '$uri';

    condition h_object is_json '  {"valid":true}  ';
    condition h_array is_json '[1,2,3]';
    condition h_string is_json '"text"';
    condition h_number is_json '-1.25e+2';
    condition h_boolean is_json true;
    condition h_null is_json null;
    condition h_trailing !is_json '{"valid":true} trailing';
    condition h_truncated !is_json '{"valid":';
    condition h_empty !is_json '';

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        when h_object h_array h_string h_number h_boolean h_null
             h_trailing h_truncated h_empty {
            access_log %%TESTDIR%%/http-json-hit.log condition_test;
        }

        location / {
            return 204;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format  condition_test  '$remote_addr';

    condition s_object is_json '  {"valid":true}  ';
    condition s_array is_json '[1,2,3]';
    condition s_string is_json '"text"';
    condition s_number is_json '-1.25e+2';
    condition s_boolean is_json true;
    condition s_null is_json null;
    condition s_trailing !is_json '{"valid":true} trailing';
    condition s_truncated !is_json '{"valid":';
    condition s_empty !is_json '';

    server {
        listen  127.0.0.1:8081;

        when s_object s_array s_string s_number s_boolean s_null
             s_trailing s_truncated s_empty {
            access_log %%TESTDIR%%/stream-json-hit.log condition_test;
        }

        return ok;
    }
}

EOF

my $output = $t->dump_config();
my $status = $?;

plan(skip_all => 'is_json support is not available')
    if $status != 0 && $output =~ /unsupported condition type "is_json"/;

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
