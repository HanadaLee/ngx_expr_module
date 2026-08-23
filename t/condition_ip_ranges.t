#!/usr/bin/perl

# Tests for IP range parsing and lookup in HTTP and Stream.

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
    ->has(qw/http stream stream_return ngx_condition_module/)
    ->plan(12);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        condition ip_single ip_range $arg_ip 127.0.0.1;
        condition ip_cidr ip_range $arg_ip 10.0.0.0/8;
        condition ip_range ip_range $arg_ip 192.0.2.10-192.0.2.20;
        condition ip_overlap ip_range $arg_ip
            192.0.2.10-192.0.2.30 192.0.2.20-192.0.2.40;
        condition ip_max ip_range $arg_ip 255.255.255.255;
        condition ip_ipv6 ip_range $arg_ip 2001:db8::/32;

        location = /single {
            when ip_single {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /cidr {
            when ip_cidr {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /range {
            when ip_range {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /overlap {
            when ip_overlap {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /max {
            when ip_max {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /ipv6 {
            when ip_ipv6 {
                error_page 418 =200 /hit;
            }
            return 418;
        }

        location = /hit {
            internal;
            return 200 'hit';
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format condition_ip_test '$remote_addr';

    server {
        listen  127.0.0.1:8081;

        condition stream_ip_range ip_range $remote_addr
            127.0.0.1-127.0.0.2;

        when stream_ip_range {
            access_log %%TESTDIR%%/stream-ip-range.log condition_ip_test;
        }

        return ok;
    }
}

EOF

$t->run();

###############################################################################

like(http_get('/single?ip=127.0.0.1'), qr/^HTTP\/1\.1 200/,
    'single IPv4 address matches');
like(http_get('/cidr?ip=10.23.45.67'), qr/^HTTP\/1\.1 200/,
    'IPv4 CIDR matches');
like(http_get('/range?ip=192.0.2.10'), qr/^HTTP\/1\.1 200/,
    'IPv4 range lower boundary matches');
like(http_get('/range?ip=192.0.2.20'), qr/^HTTP\/1\.1 200/,
    'IPv4 range upper boundary matches');
like(http_get('/range?ip=192.0.2.9'), qr/^HTTP\/1\.1 418/,
    'IPv4 range gap does not match');
like(http_get('/overlap?ip=192.0.2.35'), qr/^HTTP\/1\.1 200/,
    'overlapping ranges match through subtree max end');
like(http_get('/overlap?ip=192.0.2.9'), qr/^HTTP\/1\.1 418/,
    'address before overlapping ranges does not match');
like(http_get('/overlap?ip=192.0.2.41'), qr/^HTTP\/1\.1 418/,
    'address after overlapping ranges does not match');
like(http_get('/max?ip=255.255.255.255'), qr/^HTTP\/1\.1 200/,
    'maximum IPv4 address matches');
like(http_get('/ipv6?ip=2001:db8::1'), qr/^HTTP\/1\.1 200/,
    'IPv6 CIDR matches');

is(stream('127.0.0.1:' . port(8081))->read(), 'ok',
    'Stream IPv4 range matches');

$t->stop();

like($t->read_file('stream-ip-range.log'), qr/127\.0\.0\.1/,
    'Stream range handler uses the interval tree');
