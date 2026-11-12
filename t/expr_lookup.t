#!/usr/bin/perl

# Tests for static list lookup and repeated expression evaluation.

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

my @cases = (
    ['string empty', 'expr selected str_in "" z "" a;', 1],
    ['string order', 'expr selected str_in abc zzzz z abc aa abc;', 1],
    ['string case', 'expr selected str_in AbC abc ABC;', 0],
    ['string insensitive', 'expr selected str_in -i AbC xyz abc ABC;', 1],
    ['string miss', 'expr selected !str_in miss abc def;', 1],
    ['number normalization', 'expr selected num_in +001.2300 -4 0 1.23 1.230;', 1],
    ['number zero', 'expr selected num_in -0.000 2 +00.00 -3;', 1],
    ['number negative', 'expr selected num_in -12.5 -12.05 -12.500 -13;', 1],
    ['number fraction', 'expr selected num_in 0.0001 0.00010 0.001 0.01;', 1],
    ['number miss', 'expr selected num_in 1.234 1.23 1.2345;', 0],
    ['invalid candidate', 'expr selected !num_in invalid 1 2;', 0],
    ['invalid after match', 'expr selected num_in 1 1 invalid;', 1],
    ['invalid before match', 'expr selected num_in 2 invalid 2;', 0],
    ['dynamic before match', <<'EOF', 0],
        expr capture ~ invalid '^(?<item>.*)$';
        expr member num_in 2 $item 2;
        expr selected and capture member;
EOF
    ['dynamic after match', <<'EOF', 1],
        expr capture ~ invalid '^(?<item>.*)$';
        expr member num_in 2 2 $item;
        expr selected and capture member;
EOF
    ['string reevaluation', <<'EOF', 1],
        expr first ~ alpha '^(?<input>.*)$';
        expr second ~ beta '^(?<input>.*)$';
        expr member str_in $input z alpha q;
        expr selected and first member second !member;
EOF
    ['number reevaluation', <<'EOF', 1],
        expr first ~ 1 '^(?<number>.*)$';
        expr second ~ 2 '^(?<number>.*)$';
        expr member num_in $number 0 1 3;
        expr selected and first member second !member;
EOF
    ['dynamic list reevaluation', <<'EOF', 1],
        expr first ~ alpha '^(?<item>.*)$';
        expr second ~ beta '^(?<item>.*)$';
        expr member str_in alpha z $item q;
        expr selected and first member second !member;
EOF
    ['regex side effects', <<'EOF', 1],
        expr first ~ alpha '^(?<item>.*)$';
        expr second ~ beta '^(?<item>.*)$';
        expr member str_eq $item alpha;
        expr selected and first member second first member;
EOF

);

my $t = Test::Nginx->new()
    ->has(qw/http rewrite stream stream_return pcre ngx_expr_module/)
    ->plan(2 * @cases + 4);

my ($http, $stream, $hits, $misses) = ('', '', '', '');

for my $i (0 .. $#cases) {
    my ($name, $config, $hit) = @{$cases[$i]};
    my $hport = 8080 + $i;
    my $sport = 8180 + $i;
    $http .= <<EOF;
    server {
        listen 127.0.0.1:$hport;
        $config
        when selected { access_log %%TESTDIR%%/http-hit.log lookup; }
        when !selected { access_log %%TESTDIR%%/http-miss.log lookup; }
        location / { return 204; }
    }
EOF
    $stream .= <<EOF;
    server {
        listen 127.0.0.1:$sport;
        $config
        when selected { access_log %%TESTDIR%%/stream-hit.log lookup; }
        when !selected { access_log %%TESTDIR%%/stream-miss.log lookup; }
        return ok;
    }
EOF
}

my $conf = <<'EOF';
%%TEST_GLOBALS%%
daemon off;
events {}
http {
    %%TEST_GLOBALS_HTTP%%
    log_format lookup '$server_port';
    %%HTTP%%
}
stream {
    %%TEST_GLOBALS_STREAM%%
    log_format lookup '$server_port';
    %%STREAM%%
}
EOF
$conf =~ s/%%HTTP%%/$http/;
$conf =~ s/%%STREAM%%/$stream/;
$t->write_file_expand('nginx.conf', $conf)->run();

my (@hhits, @hmisses, @shits, @smisses);
for my $i (0 .. $#cases) {
    my ($name, undef, $hit) = @{$cases[$i]};
    like(http_get('/', PeerAddr => '127.0.0.1:' . port(8080 + $i)),
        qr/^HTTP\/1\.1 204/, "HTTP $name");
    is(stream('127.0.0.1:' . port(8180 + $i))->read(), 'ok', "Stream $name");
    push @{$hit ? \@hhits : \@hmisses}, port(8080 + $i);
    push @{$hit ? \@shits : \@smisses}, port(8180 + $i);
}
$t->stop();

is($t->read_file('http-hit.log'), join('', map { "$_\n" } @hhits), 'HTTP matches');
is($t->read_file('http-miss.log'), join('', map { "$_\n" } @hmisses), 'HTTP misses');
is($t->read_file('stream-hit.log'), join('', map { "$_\n" } @shits), 'Stream matches');
is($t->read_file('stream-miss.log'), join('', map { "$_\n" } @smisses), 'Stream misses');

###############################################################################
