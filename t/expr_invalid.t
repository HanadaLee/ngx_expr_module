#!/usr/bin/perl

# Tests for ngx_expr_module configuration errors.

###############################################################################

use warnings;
use strict;

use Test::More;

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http stream ngx_expr_module/)
    ->plan(14);

check_invalid('double func negation', 'http',
    'expr broken !!= a b;',
    qr/unsupported expr type "!!="/);

check_invalid('removed is_not_empty func', 'http',
    'expr broken is_not_empty value;',
    qr/unsupported expr type "is_not_empty"/);

check_invalid('removed str_ne func', 'http',
    'expr broken str_ne a b;',
    qr/unsupported expr type "str_ne"/);

check_invalid('removed num_ne func', 'stream',
    'expr broken num_ne 1 2;',
    qr/unsupported expr type "num_ne"/);

check_invalid('undefined expr reference', 'http',
    "expr known bool true;\nexpr broken and known missing;",
    qr/expr "missing" is not defined/);

check_invalid('expr cycle', 'http',
    "expr first not second;\nexpr second not first;",
    qr/cycle detected in expr/);

check_invalid('same-name type conflict', 'stream',
    "expr same bool true;\nexpr same = a a;",
    qr/has conflicting types/);

undef $t;

###############################################################################

sub check_invalid {
    my ($name, $context, $body, $expected) = @_;

    $t->write_file_expand('nginx.conf', <<EOF);

%%TEST_GLOBALS%%

daemon off;

events {
}

$context {
$body
}

EOF

    my $output = $t->dump_config();
    my $status = $?;

    isnt($status, 0, "$name is rejected");
    like($output, $expected, "$name reports the expected error");
}

###############################################################################
