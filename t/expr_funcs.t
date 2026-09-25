#!/usr/bin/perl

# Tests for ngx_expr_module funcs in HTTP and Stream.

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
    ->has(qw/http stream stream_return pcre ngx_expr_module/)
    ->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format  expr_test  '$uri:$status';

    expr root_scope bool true;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        expr h_false bool false;
        expr h01 !bool false;
        expr h02 is_empty "";
        expr h03 !is_empty value;
        expr h04 = abc abc;
        expr h05 != abc xyz;
        expr h06 = -i AbC abc;
        expr h07 ^~ abc ab;
        expr h08 !^~ abc xy;
        expr h09 ~$ abc bc;
        expr h10 !~$ abc xy;
        expr h11 str_contains abc b;
        expr h12 !str_contains abc z;
        expr h13 ~ abc ^a;
        expr h14 !~ abc ^z;
        expr h15 ~* AbC ^a;
        expr h16 !~* AbC ^z;
        expr h17 str_in abc def abc;
        expr h18 !str_in abc def xyz;
        expr h19 not str_eq abc xyz;
        expr h20 is_num 1.25;
        expr h21 !is_num abc;
        expr h22 == 1 1.0;
        expr h23 !== 1 2;
        expr h24 < 1 2;
        expr h25 !< 2 1;
        expr h26 <= 1 1;
        expr h27 !<= 2 1;
        expr h28 > 2 1;
        expr h29 !> 1 2;
        expr h30 >= 2 2;
        expr h31 !>= 1 2;
        expr h32 num_range 5 1 5;
        expr h33 !num_range 5 1 4;
        expr h34 num_in 2 1 2.0;
        expr h35 !num_in 3 1 2;
        expr h36 time_range 0 gmt year=1970 month=1 day=1 wday=4
                                         hour=0 min=0 sec=0;
        expr h37 !time_range 0 gmt year=2000;
        expr h38 is_ip 127.0.0.1;
        expr h39 !is_ip invalid;
        expr h40 is_cidr 127.0.0.0/8;
        expr h41 !is_cidr invalid;
        expr h42 ip_range 127.0.0.1 127.0.0.0/8;
        expr h43 !ip_range 10.0.0.1 127.0.0.0/8;
        expr h44 and h01 h04 h22;
        expr h45 or h_false h04;
        expr h46 not h_false;
        expr h47 = first second;
        expr h47 = first first;
        expr h48 str_eq abc abc;
        expr h49 !str_eq abc xyz;
        expr h50 str_starts_with abc ab;
        expr h51 !str_starts_with abc xy;
        expr h52 str_ends_with abc bc;
        expr h53 !str_ends_with abc xy;
        expr h54 str_regex_match abc ^a;
        expr h55 !str_regex_match abc ^z;
        expr h56 str_regex_match -i AbC ^a;
        expr h57 !str_regex_match -i AbC ^z;
        expr h58 num_eq 1 1.0;
        expr h59 !num_eq 1 2;
        expr h60 num_lt 1 2;
        expr h61 !num_lt 2 1;
        expr h62 num_le 1 1;
        expr h63 !num_le 2 1;
        expr h64 num_gt 2 1;
        expr h65 !num_gt 1 2;
        expr h66 num_ge 2 2;
        expr h67 !num_ge 1 2;

        expr h_all and h01 h02 h03 h04 h05 h06 h07 h08 h09 h10
                            h11 h12 h13 h14 h15 h16 h17 h18 h19 h20
                            h21 h22 h23 h24 h25 h26 h27 h28 h29 h30
                            h31 h32 h33 h34 h35 h36 h37 h38 h39 h40
                            h41 h42 h43 h44 h45 h46 h47 h48 h49 h50
                            h51 h52 h53 h54 h55 h56 h57 h58 h59 h60
                            h61 h62 h63 h64 h65 h66 h67;

        expr scope_flag bool true;

        location = /matrix {
            when h_all {
                access_log %%TESTDIR%%/http-matrix-hit.log expr_test;
            }

            when h_false {
                access_log %%TESTDIR%%/http-matrix-miss.log expr_test;
            }

            return 204;
        }

        location = /scope {
            expr scope_flag bool false;

            when root_scope !scope_flag {
                access_log %%TESTDIR%%/http-scope-hit.log expr_test;
            }

            return 204;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format  expr_test  '$remote_addr';

    server {
        listen  127.0.0.1:8081;

        expr s_false bool false;
        expr s01 !bool false;
        expr s02 is_empty "";
        expr s03 !is_empty value;
        expr s04 = abc abc;
        expr s05 != abc xyz;
        expr s06 = -i AbC abc;
        expr s07 ^~ abc ab;
        expr s08 !^~ abc xy;
        expr s09 ~$ abc bc;
        expr s10 !~$ abc xy;
        expr s11 str_contains abc b;
        expr s12 !str_contains abc z;
        expr s13 ~ abc ^a;
        expr s14 !~ abc ^z;
        expr s15 ~* AbC ^a;
        expr s16 !~* AbC ^z;
        expr s17 str_in abc def abc;
        expr s18 !str_in abc def xyz;
        expr s19 not str_eq abc xyz;
        expr s20 is_num 1.25;
        expr s21 !is_num abc;
        expr s22 == 1 1.0;
        expr s23 !== 1 2;
        expr s24 < 1 2;
        expr s25 !< 2 1;
        expr s26 <= 1 1;
        expr s27 !<= 2 1;
        expr s28 > 2 1;
        expr s29 !> 1 2;
        expr s30 >= 2 2;
        expr s31 !>= 1 2;
        expr s32 num_range 5 1 5;
        expr s33 !num_range 5 1 4;
        expr s34 num_in 2 1 2.0;
        expr s35 !num_in 3 1 2;
        expr s36 time_range 0 gmt year=1970 month=1 day=1 wday=4
                                         hour=0 min=0 sec=0;
        expr s37 !time_range 0 gmt year=2000;
        expr s38 is_ip 127.0.0.1;
        expr s39 !is_ip invalid;
        expr s40 is_cidr 127.0.0.0/8;
        expr s41 !is_cidr invalid;
        expr s42 ip_range 127.0.0.1 127.0.0.0/8;
        expr s43 !ip_range 10.0.0.1 127.0.0.0/8;
        expr s44 and s01 s04 s22;
        expr s45 or s_false s04;
        expr s46 not s_false;
        expr s47 = first second;
        expr s47 = first first;
        expr s48 str_eq abc abc;
        expr s49 !str_eq abc xyz;
        expr s50 str_starts_with abc ab;
        expr s51 !str_starts_with abc xy;
        expr s52 str_ends_with abc bc;
        expr s53 !str_ends_with abc xy;
        expr s54 str_regex_match abc ^a;
        expr s55 !str_regex_match abc ^z;
        expr s56 str_regex_match -i AbC ^a;
        expr s57 !str_regex_match -i AbC ^z;
        expr s58 num_eq 1 1.0;
        expr s59 !num_eq 1 2;
        expr s60 num_lt 1 2;
        expr s61 !num_lt 2 1;
        expr s62 num_le 1 1;
        expr s63 !num_le 2 1;
        expr s64 num_gt 2 1;
        expr s65 !num_gt 1 2;
        expr s66 num_ge 2 2;
        expr s67 !num_ge 1 2;

        expr s_all and s01 s02 s03 s04 s05 s06 s07 s08 s09 s10
                            s11 s12 s13 s14 s15 s16 s17 s18 s19 s20
                            s21 s22 s23 s24 s25 s26 s27 s28 s29 s30
                            s31 s32 s33 s34 s35 s36 s37 s38 s39 s40
                            s41 s42 s43 s44 s45 s46 s47 s48 s49 s50
                            s51 s52 s53 s54 s55 s56 s57 s58 s59 s60
                            s61 s62 s63 s64 s65 s66 s67;

        when s_all {
            access_log %%TESTDIR%%/stream-matrix-hit.log expr_test;
        }

        when s_false {
            access_log %%TESTDIR%%/stream-matrix-miss.log expr_test;
        }

        return ok;
    }
}

EOF

$t->run();

###############################################################################

like(http_get('/matrix'), qr/^HTTP\/1\.1 204/, 'HTTP func matrix');
like(http_get('/scope'), qr/^HTTP\/1\.1 204/, 'HTTP expr scope');
is(stream('127.0.0.1:' . port(8081))->read(), 'ok',
    'Stream func matrix');

$t->stop();

is($t->read_file('http-matrix-hit.log'), "/matrix:204\n",
    'HTTP true expression enables access_log');
is($t->read_file('http-matrix-miss.log'), '',
    'HTTP false expression suppresses access_log');
is($t->read_file('http-scope-hit.log'), "/scope:204\n",
    'HTTP location expr overrides inherited definition');
is($t->read_file('stream-matrix-hit.log'), "127.0.0.1\n",
    'Stream true expression enables access_log');
is($t->read_file('stream-matrix-miss.log'), '',
    'Stream false expression suppresses access_log');

###############################################################################
