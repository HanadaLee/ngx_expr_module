#!/usr/bin/perl

# Tests for inherited expression graphs and local overrides.

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
    ->has(qw/http rewrite stream stream_return ngx_expr_module/)->plan(15);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format scope '$server_port:$uri';

    expr leaf bool true;
    expr left and leaf leaf;
    expr right and leaf leaf;
    expr selected and left right;

    when selected { access_log %%TESTDIR%%/http-hit.log scope; }
    when !selected { access_log %%TESTDIR%%/http-miss.log scope; }

    server {
        listen 127.0.0.1:8080;

        location / { return 204; }

        location = /off {
            expr leaf bool false;
            return 204;
        }

        location /nested/ {
            expr leaf bool false;

            location /nested/inherit { return 204; }

            location /nested/restore {
                expr leaf bool true;
                return 204;
            }
        }

        location = /local {
            expr selected and local local;
            expr local bool true;
            return 204;
        }

        location = /late {
            expr late bool true;
            return 204;
        }
    }

    server {
        listen 127.0.0.1:8081;
        expr leaf bool false;
        location / { return 204; }
    }

    server {
        listen 127.0.0.1:8082;
        location / { return 204; }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format scope '$server_port';

    expr leaf bool true;
    expr left and leaf leaf;
    expr right and leaf leaf;
    expr selected and left right;

    when selected { access_log %%TESTDIR%%/stream-hit.log scope; }
    when !selected { access_log %%TESTDIR%%/stream-miss.log scope; }

    server {
        listen 127.0.0.1:8083;
        return ok;
    }

    server {
        listen 127.0.0.1:8084;
        expr leaf bool false;
        return ok;
    }

    server {
        listen 127.0.0.1:8085;
        expr selected and local local;
        expr local bool true;
        return ok;
    }
}

EOF

$t->run();

###############################################################################

my @requests = (
    [8080, '/', 1],
    [8080, '/off', 0],
    [8080, '/nested/inherit', 0],
    [8080, '/nested/restore', 1],
    [8080, '/local', 1],
    [8080, '/late', 1],
    [8081, '/', 0],
    [8082, '/', 1],
);

my ($hits, $misses) = ('', '');
for my $request (@requests) {
    my ($port, $uri, $hit) = @$request;

    like(http_get($uri, PeerAddr => '127.0.0.1:' . port($port)),
        qr/^HTTP\/1\.1 204/, "HTTP scope $port $uri");

    my $entry = port($port) . ":$uri\n";
    if ($hit) {
        $hits .= $entry;
    } else {
        $misses .= $entry;
    }
}

for my $port (8083 .. 8085) {
    is(stream('127.0.0.1:' . port($port))->read(), 'ok', "Stream scope $port");
}

$t->stop();

is($t->read_file('http-hit.log'), $hits,
    'HTTP inherited graph observes local overrides and restores parent values');
is($t->read_file('http-miss.log'), $misses,
    'HTTP local overrides do not change sibling scopes');
is($t->read_file('stream-hit.log'), port(8083) . "\n" . port(8085) . "\n",
    'Stream inherits the parent graph and resolves local forward references');
is($t->read_file('stream-miss.log'), port(8084) . "\n",
    'Stream local override does not change inherited graphs');

###############################################################################
