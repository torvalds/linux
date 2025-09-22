#!/usr/bin/perl
#	$OpenBSD: httpd.pl,v 1.2 2016/05/03 19:13:04 bluhm Exp $

# Copyright (c) 2010-2015 Alexander Bluhm <bluhm@openbsd.org>
# Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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
use Httpd;
require 'funcs.pl';

sub usage {
	die "usage: httpd.pl chroot [test-args.pl]\n";
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
my($d, $c);
$d = Httpd->new(
    chroot              => $ARGV[0],
    listendomain        => AF_INET,
    listenaddr          => "127.0.0.1",
    listenport          => $rport,
    connectdomain       => AF_INET,
    connectaddr         => "127.0.0.1",
    connectport         => $sport,
    %{$args{httpd}},
    testfile            => $testfile,
);
$c = Client->new(
    chroot              => $ARGV[0],
    func                => \&http_client,
    connectdomain       => AF_INET,
    connectaddr         => "127.0.0.1",
    connectport         => $rport,
    %{$args{client}},
    testfile            => $testfile,
) unless $args{client}{noclient};

$d->run;
$d->up;
$c->run->up unless $args{client}{noclient};

$c->down unless $args{client}{noclient};
$d->kill_child;
$d->down;

check_logs($c, $d, %args);
