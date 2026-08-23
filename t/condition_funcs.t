#!/usr/bin/perl

# Tests for ngx_condition_module funcs in HTTP and Stream.

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
    ->has(qw/http stream stream_return pcre ngx_condition_module/)
    ->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    log_format  condition_test  '$uri:$status';

    condition root_scope bool true;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        condition h_false bool false;
        condition h01 !bool false;
        condition h02 is_empty "";
        condition h03 !is_empty value;
        condition h04 = abc abc;
        condition h05 != abc xyz;
        condition h06 = -i AbC abc;
        condition h07 ^~ abc ab;
        condition h08 !^~ abc xy;
        condition h09 ~$ abc bc;
        condition h10 !~$ abc xy;
        condition h11 str_contains abc b;
        condition h12 !str_contains abc z;
        condition h13 ~ abc ^a;
        condition h14 !~ abc ^z;
        condition h15 ~* AbC ^a;
        condition h16 !~* AbC ^z;
        condition h17 str_in abc def abc;
        condition h18 !str_in abc def xyz;
        condition h19 not str_eq abc xyz;
        condition h20 is_num 1.25;
        condition h21 !is_num abc;
        condition h22 == 1 1.0;
        condition h23 !== 1 2;
        condition h24 < 1 2;
        condition h25 !< 2 1;
        condition h26 <= 1 1;
        condition h27 !<= 2 1;
        condition h28 > 2 1;
        condition h29 !> 1 2;
        condition h30 >= 2 2;
        condition h31 !>= 1 2;
        condition h32 num_range 5 1 5;
        condition h33 !num_range 5 1 4;
        condition h34 num_in 2 1 2.0;
        condition h35 !num_in 3 1 2;
        condition h36 time_range 0 gmt year=1970 month=1 day=1 wday=4
                                         hour=0 min=0 sec=0;
        condition h37 !time_range 0 gmt year=2000;
        condition h38 is_ip 127.0.0.1;
        condition h39 !is_ip invalid;
        condition h40 is_cidr 127.0.0.0/8;
        condition h41 !is_cidr invalid;
        condition h42 ip_range 127.0.0.1 127.0.0.0/8;
        condition h43 !ip_range 10.0.0.1 127.0.0.0/8;
        condition h44 and h01 h04 h22;
        condition h45 or h_false h04;
        condition h46 not h_false;
        condition h47 = first second;
        condition h47 = first first;
        condition h48 str_eq abc abc;
        condition h49 !str_eq abc xyz;
        condition h50 str_starts_with abc ab;
        condition h51 !str_starts_with abc xy;
        condition h52 str_ends_with abc bc;
        condition h53 !str_ends_with abc xy;
        condition h54 str_regex_match abc ^a;
        condition h55 !str_regex_match abc ^z;
        condition h56 str_regex_match -i AbC ^a;
        condition h57 !str_regex_match -i AbC ^z;
        condition h58 num_eq 1 1.0;
        condition h59 !num_eq 1 2;
        condition h60 num_lt 1 2;
        condition h61 !num_lt 2 1;
        condition h62 num_le 1 1;
        condition h63 !num_le 2 1;
        condition h64 num_gt 2 1;
        condition h65 !num_gt 1 2;
        condition h66 num_ge 2 2;
        condition h67 !num_ge 1 2;

        condition h_all and h01 h02 h03 h04 h05 h06 h07 h08 h09 h10
                            h11 h12 h13 h14 h15 h16 h17 h18 h19 h20
                            h21 h22 h23 h24 h25 h26 h27 h28 h29 h30
                            h31 h32 h33 h34 h35 h36 h37 h38 h39 h40
                            h41 h42 h43 h44 h45 h46 h47 h48 h49 h50
                            h51 h52 h53 h54 h55 h56 h57 h58 h59 h60
                            h61 h62 h63 h64 h65 h66 h67;

        condition scope_flag bool true;

        location = /matrix {
            when h_all {
                access_log %%TESTDIR%%/http-matrix-hit.log condition_test;
            }

            when h_false {
                access_log %%TESTDIR%%/http-matrix-miss.log condition_test;
            }

            return 204;
        }

        location = /scope {
            condition scope_flag bool false;

            when root_scope !scope_flag {
                access_log %%TESTDIR%%/http-scope-hit.log condition_test;
            }

            return 204;
        }
    }
}

stream {
    %%TEST_GLOBALS_STREAM%%

    log_format  condition_test  '$remote_addr';

    server {
        listen  127.0.0.1:8081;

        condition s_false bool false;
        condition s01 !bool false;
        condition s02 is_empty "";
        condition s03 !is_empty value;
        condition s04 = abc abc;
        condition s05 != abc xyz;
        condition s06 = -i AbC abc;
        condition s07 ^~ abc ab;
        condition s08 !^~ abc xy;
        condition s09 ~$ abc bc;
        condition s10 !~$ abc xy;
        condition s11 str_contains abc b;
        condition s12 !str_contains abc z;
        condition s13 ~ abc ^a;
        condition s14 !~ abc ^z;
        condition s15 ~* AbC ^a;
        condition s16 !~* AbC ^z;
        condition s17 str_in abc def abc;
        condition s18 !str_in abc def xyz;
        condition s19 not str_eq abc xyz;
        condition s20 is_num 1.25;
        condition s21 !is_num abc;
        condition s22 == 1 1.0;
        condition s23 !== 1 2;
        condition s24 < 1 2;
        condition s25 !< 2 1;
        condition s26 <= 1 1;
        condition s27 !<= 2 1;
        condition s28 > 2 1;
        condition s29 !> 1 2;
        condition s30 >= 2 2;
        condition s31 !>= 1 2;
        condition s32 num_range 5 1 5;
        condition s33 !num_range 5 1 4;
        condition s34 num_in 2 1 2.0;
        condition s35 !num_in 3 1 2;
        condition s36 time_range 0 gmt year=1970 month=1 day=1 wday=4
                                         hour=0 min=0 sec=0;
        condition s37 !time_range 0 gmt year=2000;
        condition s38 is_ip 127.0.0.1;
        condition s39 !is_ip invalid;
        condition s40 is_cidr 127.0.0.0/8;
        condition s41 !is_cidr invalid;
        condition s42 ip_range 127.0.0.1 127.0.0.0/8;
        condition s43 !ip_range 10.0.0.1 127.0.0.0/8;
        condition s44 and s01 s04 s22;
        condition s45 or s_false s04;
        condition s46 not s_false;
        condition s47 = first second;
        condition s47 = first first;
        condition s48 str_eq abc abc;
        condition s49 !str_eq abc xyz;
        condition s50 str_starts_with abc ab;
        condition s51 !str_starts_with abc xy;
        condition s52 str_ends_with abc bc;
        condition s53 !str_ends_with abc xy;
        condition s54 str_regex_match abc ^a;
        condition s55 !str_regex_match abc ^z;
        condition s56 str_regex_match -i AbC ^a;
        condition s57 !str_regex_match -i AbC ^z;
        condition s58 num_eq 1 1.0;
        condition s59 !num_eq 1 2;
        condition s60 num_lt 1 2;
        condition s61 !num_lt 2 1;
        condition s62 num_le 1 1;
        condition s63 !num_le 2 1;
        condition s64 num_gt 2 1;
        condition s65 !num_gt 1 2;
        condition s66 num_ge 2 2;
        condition s67 !num_ge 1 2;

        condition s_all and s01 s02 s03 s04 s05 s06 s07 s08 s09 s10
                            s11 s12 s13 s14 s15 s16 s17 s18 s19 s20
                            s21 s22 s23 s24 s25 s26 s27 s28 s29 s30
                            s31 s32 s33 s34 s35 s36 s37 s38 s39 s40
                            s41 s42 s43 s44 s45 s46 s47 s48 s49 s50
                            s51 s52 s53 s54 s55 s56 s57 s58 s59 s60
                            s61 s62 s63 s64 s65 s66 s67;

        when s_all {
            access_log %%TESTDIR%%/stream-matrix-hit.log condition_test;
        }

        when s_false {
            access_log %%TESTDIR%%/stream-matrix-miss.log condition_test;
        }

        return ok;
    }
}

EOF

$t->run();

###############################################################################

like(http_get('/matrix'), qr/^HTTP\/1\.1 204/, 'HTTP func matrix');
like(http_get('/scope'), qr/^HTTP\/1\.1 204/, 'HTTP condition scope');
is(stream('127.0.0.1:' . port(8081))->read(), 'ok',
    'Stream func matrix');

$t->stop();

is($t->read_file('http-matrix-hit.log'), "/matrix:204\n",
    'HTTP true expression enables access_log');
is($t->read_file('http-matrix-miss.log'), '',
    'HTTP false expression suppresses access_log');
is($t->read_file('http-scope-hit.log'), "/scope:204\n",
    'HTTP location condition overrides inherited definition');
is($t->read_file('stream-matrix-hit.log'), "127.0.0.1\n",
    'Stream true expression enables access_log');
is($t->read_file('stream-matrix-miss.log'), '',
    'Stream false expression suppresses access_log');

###############################################################################
