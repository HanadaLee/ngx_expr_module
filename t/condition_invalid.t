#!/usr/bin/perl

# Tests for ngx_condition_module configuration errors.

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
    ->has(qw/http stream ngx_condition_module/)
    ->plan(14);

check_invalid('double operator negation', 'http',
    'condition broken !!= a b;',
    qr/unsupported condition type "!!="/);

check_invalid('removed is_not_empty operator', 'http',
    'condition broken is_not_empty value;',
    qr/unsupported condition type "is_not_empty"/);

check_invalid('removed str_ne operator', 'http',
    'condition broken str_ne a b;',
    qr/unsupported condition type "str_ne"/);

check_invalid('removed num_ne operator', 'stream',
    'condition broken num_ne 1 2;',
    qr/unsupported condition type "num_ne"/);

check_invalid('undefined condition reference', 'http',
    "condition known bool true;\ncondition broken and known missing;",
    qr/condition "missing" is not defined/);

check_invalid('condition cycle', 'http',
    "condition first not second;\ncondition second not first;",
    qr/cycle detected in condition/);

check_invalid('same-name type conflict', 'stream',
    "condition same bool true;\ncondition same = a a;",
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
