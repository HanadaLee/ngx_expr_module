#!/usr/bin/perl

# Tests for the optional ngx_condition_module cJSON operator.

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

    condition h_json is_json '{"valid":true}';
    condition h_not_json !is_json invalid;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        when h_json h_not_json {
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

    condition s_json is_json '[1,2,3]';
    condition s_not_json !is_json invalid;

    server {
        listen  127.0.0.1:8081;

        when s_json s_not_json {
            access_log %%TESTDIR%%/stream-json-hit.log condition_test;
        }

        return ok;
    }
}

EOF

my $output = $t->dump_config();
my $status = $?;

plan(skip_all => 'cJSON library not available')
    if $status != 0 && $output =~ /unsupported condition type "is_json"/;

BAIL_OUT("failed to validate cJSON test configuration:\n$output")
    if $status != 0;

$t->plan(4)->run();

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
