#!/usr/bin/perl

# Time complex values must behave identically with and without core extensions.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use Test::Nginx qw/ :DEFAULT http_content /;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite ngx_expr_module
	ngx_http_expr_time_test_module/);

my @literal = (
	['zero', '0', '0', '0|0'],
	['bare', '2', '3', '2000|3'],
	['milliseconds', '1500ms', '2s', '1500|2'],
	['compound', '1m30s', '1h2m', '90000|3720'],
	['days', '2d', '1w', '172800000|604800'],
);
my @invalid = (
	['expr_time_msec', '-1s'],
	['expr_time_sec', '-1s'],
	['expr_time_msec', '1.5s'],
	['expr_time_sec', '1ms'],
	['expr_time_msec', 'wrong'],
	['expr_time_sec', 'wrong'],
	['expr_time_msec', '999999999999999999999999s'],
	['expr_time_sec', '999999999999999999999999s'],
);

$t->plan(scalar(@literal) + 16 + 2 * scalar(@invalid) + 4);

my $locations = '';
for my $case (@literal) {
	my ($name, $msec, $sec) = @$case;
	$locations .= "location = /$name { expr_time_msec $msec; "
		. "expr_time_sec $sec; expr_time_test; }\n";
}

my $config = <<'EOF';
%%TEST_GLOBALS%%
daemon off;
events {}
http {
    %%TEST_GLOBALS_HTTP%%
    expr selected str_eq $arg_mode hit;

    server {
        listen 127.0.0.1:8080;
        LITERALS

        location = /default {
            expr_time_test;
        }
        location = /dynamic {
            expr_time_msec $arg_value;
            expr_time_sec $arg_value;
            expr_time_test;
        }
        location = /conditional {
            when selected {
                expr_time_msec $arg_value;
                expr_time_sec $arg_value;
            }
            expr_time_msec 7s;
            expr_time_sec 11s;
            expr_time_test;
        }
        location = /miss {
            when selected {
                expr_time_msec 1s;
                expr_time_sec 2s;
            }
            expr_time_test;
        }
        location /inherit/ {
            expr_time_msec 3s;
            expr_time_sec 4s;
            location = /inherit/child {
                expr_time_sec 9s;
                expr_time_test;
            }
        }
        location = /repeat-value {
            expr_time_msec $arg_value;
            expr_time_sec $arg_value;
            expr_time_args value=2s;
            expr_time_test;
        }
        location = /repeat-when {
            when selected {
                expr_time_msec 3s;
                expr_time_sec 4s;
            }
            expr_time_msec 5s;
            expr_time_sec 6s;
            expr_time_args mode=miss;
            expr_time_test;
        }
    }
}
EOF
$config =~ s/LITERALS/$locations/;
$t->write_file_expand('nginx.conf', $config);
$t->run();

for my $case (@literal) {
	is(body('/' . $case->[0]), $case->[3], 'literal ' . $case->[0]);
}
is(body('/default'), '17|19', 'missing values use caller defaults');
is(body('/dynamic?value=2s'), '2000|2', 'variable values use requested units');
is(body('/dynamic?value=1m30s'), '90000|90', 'variable compound units');
is(body('/dynamic?value=1500ms'), '1500|19', 'seconds reject millisecond units');
is(body('/dynamic?value=wrong'), '17|19', 'invalid variables use caller defaults');
is(body('/dynamic?value=-1s'), '17|19', 'negative variables use caller defaults');
is(body('/dynamic?value=999999999999999999999999s'), '17|19',
	'overflowing variables use caller defaults');
is(body('/dynamic'), '17|19', 'empty variables use caller defaults');
is(body('/conditional?mode=hit&value=6s'), '6000|6', 'matching conditional entry');
is(body('/conditional?mode=miss&value=6s'), '7000|11', 'unconditional fallback');
is(body('/miss?mode=miss'), '17|19', 'condition miss uses caller defaults');
is(body('/inherit/child'), '3000|9', 'time fields inherit independently');
is(body('/repeat-value?value=1s'), '1000|1|2000|2',
	'complex values are evaluated again in the same request');
is(body('/repeat-when?mode=hit'), '3000|4|5000|6',
	'when is evaluated again in the same request');

$t->stop();
my $error = $t->read_file('error.log');
like($error, qr/invalid msec "wrong"/, 'milliseconds report invalid runtime input');
like($error, qr/invalid sec "wrong"/, 'seconds report invalid runtime input');

for my $case (@invalid) {
	my ($directive, $value) = @$case;
	write_config($t, "$directive $value;");
	my $output = $t->dump_config();
	isnt($?, 0, "$directive rejects $value");
	like($output, qr/invalid value/, 'configuration diagnostic matches core');
}

for my $directive (qw/expr_time_msec expr_time_sec/) {
	write_config($t, "when selected { $directive 1s; $directive 2s; }");
	my $output = $t->dump_config();
	isnt($?, 0, "$directive rejects duplicate conditional slots");
	like($output, qr/is duplicate/, 'duplicate diagnostic matches core');
}

sub body {
	my $body = http_content(http_get(shift));
	$body =~ s/\n$//;
	return $body;
}

sub write_config {
	my ($test, $directives) = @_;
	$test->write_file_expand('nginx.conf', <<EOF);
%%TEST_GLOBALS%%
daemon off;
events {}
http {
    %%TEST_GLOBALS_HTTP%%
    expr selected bool true;
    server { listen 127.0.0.1:8080; location / { $directives } }
}
EOF
}
