#!/usr/bin/perl

# Tests for inherited expression index pages and local overrides.

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
    ['page override', <<'EOF', 1],
        expr item63 bool false;
        expr item64 bool false;
        expr item127 bool false;
        expr item128 bool false;
        expr selected and item0 item62 !item63 !item64 item65 item126
                          !item127 !item128 item129 item255;
EOF
    ['page sibling', 'expr selected and item0 item63 item64 item127 item128 item255;', 1],
);

my $t = Test::Nginx->new()
    ->has(qw/http rewrite stream stream_return pcre ngx_expr_module/)
    ->plan(2 * @cases + 4);

my $definitions = join '', map { "expr item$_ bool true;\n" } 0 .. 255;
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
    %%DEFINITIONS%%
    %%HTTP%%
}
stream {
    %%TEST_GLOBALS_STREAM%%
    log_format lookup '$server_port';
    %%DEFINITIONS%%
    %%STREAM%%
}
EOF
$conf =~ s/%%DEFINITIONS%%/$definitions/g;
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
