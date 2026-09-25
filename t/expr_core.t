#!/usr/bin/perl

# Core expr/when syntax works without expression-aware external directives.

use warnings;
use strict;

use Test::More;

use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http stream stream_return ngx_expr_module/)
    ->plan(2);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    expr global bool true;

    server {
        listen 127.0.0.1:8080;

        expr server_expr and global local;

        location / {
            expr local bool true;

            when server_expr {
            }

            return 204;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    expr global bool true;

    server {
        listen 127.0.0.1:8081;

        expr local bool true;

        when global local {
        }

        return ok;
    }
}

EOF

$t->run();

like(http_get('/'), qr/^HTTP\/1\.1 204/, 'HTTP expr and when parse');
is(stream('127.0.0.1:' . port(8081))->read(), 'ok',
    'Stream expr and when parse');

$t->stop();
