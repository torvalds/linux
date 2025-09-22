#!/usr/bin/perl
#	$OpenBSD: echo.pl,v 1.2 2017/10/27 16:59:14 bluhm Exp $

# Copyright (c) 2010-2013 Alexander Bluhm <bluhm@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use strict;
use warnings;
use Socket;
use Socket6;

use Child;
use Client;
use Server;
require 'funcs.pl';

sub usage {
	die "usage: echo.pl copy|splice [args-test.pl]\n";
}

my $test;
our %args;
if (@ARGV and -f $ARGV[-1]) {
	$test = pop;
	do $test
	    or die "Do test file $test failed: ", $@ || $!;
}
@ARGV == 1 or usage();

exit 0 if $args{noecho} || $args{client}{alarm} || $args{server}{alarm};

my $r = Server->new(
    forward		=> $ARGV[0],
    func		=> \&relay,
    logfile		=> "relay.log",
    listendomain	=> AF_INET,
    listenaddr		=> "127.0.0.1",
    %{$args{relay}},
);
my $s = Child->new(
    logfile		=> "server.log",
    oobinline		=> 1,
    %{$args{server}},
    func		=> sub {
	($args{server}{func} || \&read_stream)->(@_);
	eval { shutout(@_) };
    },
);
my $c = Client->new(
    connectdomain	=> AF_INET,
    connectaddr		=> "127.0.0.1",
    connectport		=> $r->{listenport},
    oobinline		=> 1,
    %{$args{client}},
    func		=> sub {
	$s->run->up;
	eval { ($args{client}{func} || \&write_stream)->(@_) };
	warn $@ if $@;
	eval { shutout(@_) };
	$s->down;
    },
);

$r->run;
$c->run->up;
$r->up;

$c->down;
$r->down;
$s->{pid} = -1;  # XXX hack
$s->down;

exit if $args{nocheck} || $args{client}{nocheck};

check_logs($c, $r, $s, %args);
