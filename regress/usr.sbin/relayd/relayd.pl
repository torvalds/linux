#!/usr/bin/perl
#	$OpenBSD: relayd.pl,v 1.15 2016/08/25 22:56:13 bluhm Exp $

# Copyright (c) 2010-2014 Alexander Bluhm <bluhm@openbsd.org>
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

use Client;
use Relayd;
use Server;
require 'funcs.pl';

sub usage {
	die "usage: relay.pl copy|splice [test-args.pl]\n";
}

my $testfile;
our %args;
if (@ARGV and -f $ARGV[-1]) {
	$testfile = pop;
	do $testfile
	    or die "Do test file $testfile failed: ", $@ || $!;
}
@ARGV == 1 or usage();

my $redo = $args{lengths} && @{$args{lengths}};
$redo = 0 if $args{client}{http_vers};  # run only one persistent connection
my($sport, $rport) = find_ports(num => 2);
my($s, $r, $c);
$s = Server->new(
    forward             => $ARGV[0],
    func                => \&read_char,
    listendomain        => AF_INET,
    listenaddr          => "127.0.0.1",
    listenport          => $sport,
    redo                => $redo,
    %{$args{server}},
    testfile            => $testfile,
    client              => \$c,
) unless $args{server}{noserver};
$r = Relayd->new(
    forward             => $ARGV[0],
    listendomain        => AF_INET,
    listenaddr          => "127.0.0.1",
    listenport          => $rport,
    connectdomain       => AF_INET,
    connectaddr         => "127.0.0.1",
    connectport         => $sport,
    %{$args{relayd}},
    testfile            => $testfile,
);
$c = Client->new(
    forward             => $ARGV[0],
    func                => \&write_char,
    connectdomain       => AF_INET,
    connectaddr         => "127.0.0.1",
    connectport         => $rport,
    %{$args{client}},
    testfile            => $testfile,
    server              => \$s,
) unless $args{client}{noclient};

$s->run unless $args{server}{noserver};
$r->run;
$r->up;
$c->run->up unless $args{client}{noclient};
$s->up unless $args{server}{noserver};

$c->down unless $args{client}{noclient};
$s->down unless $args{server}{noserver};
$r->kill_child;
$r->down;

check_logs($c, $r, $s, %args);
