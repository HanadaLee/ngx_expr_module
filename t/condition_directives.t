#!/usr/bin/perl

# Tests for condition-aware nginx HTTP directives.

###############################################################################

use warnings;
use strict;

use Test::More;

use lib 'lib';
use Test::Nginx qw/ :DEFAULT http_content /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http gzip sub ngx_condition_module/)
    ->plan(7);

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

        condition do_gzip = $arg_gzip 1;
        condition do_sub = $arg_sub 1;
        condition do_error = $arg_error 1;

        when do_gzip {
            gzip on;
            gzip_comp_level 1;
            gzip_min_length 1;
        }

        gzip off;
        gzip_min_length 1;
        gzip_types text/plain;
        sub_filter_types text/plain;
        sub_filter_once off;

        when do_sub {
            sub_filter needle replaced;
        }

        when do_error {
            error_page 418 =200 /handled;
        }

        location = /gzip {
            default_type text/plain;
            return 200 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
                        bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
                        cccccccccccccccccccccccccccccccccccccccccccccccccc';
        }

        location = /sub {
            default_type text/plain;
            return 200 'before needle after';
        }

        location = /error {
            return 418;
        }

        location = /handled {
            internal;
            return 200 'handled';
        }
    }
}

EOF

$t->run();

###############################################################################

is(http_content(http_get('/sub?sub=1')), 'before replaced after',
    'conditional sub_filter applies on match');
is(http_content(http_get('/sub?sub=0')), 'before needle after',
    'conditional sub_filter is skipped on miss');

like(http_gzip('/gzip?gzip=1'), qr/^Content-Encoding: gzip/m,
    'conditional gzip applies on match');
unlike(http_gzip('/gzip?gzip=0'), qr/^Content-Encoding: gzip/m,
    'conditional gzip is skipped on miss');

my $response = http_get('/error?error=1');
like($response, qr/^HTTP\/1\.1 200/, 'conditional error_page applies on match');
is(http_content($response), 'handled', 'conditional error_page target body');
like(http_get('/error?error=0'), qr/^HTTP\/1\.1 418/,
    'conditional error_page is skipped on miss');

###############################################################################

sub http_gzip {
    my ($uri) = @_;

    return http(<<EOF);
GET $uri HTTP/1.1
Host: localhost
Connection: close
Accept-Encoding: gzip

EOF
}

###############################################################################
