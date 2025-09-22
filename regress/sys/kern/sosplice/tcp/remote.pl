#!/usr/bin/perl
#	$OpenBSD: remote.pl,v 1.3 2017/10/27 16:59:14 bluhm Exp $

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
use File::Basename;
use File::Copy;
use Socket;
use Socket6;

use Client;
use Relay;
use Server;
use Remote;
require 'funcs.pl';

sub usage {
	die <<"EOF";
usage:
    remote.pl localport remoteaddr remoteport [args-test.pl]
	Run test with local client and server.  Remote relay
	forwarding from remoteaddr remoteport to server localport
	has to be started manually.
    remote.pl copy|splice listenaddr connectaddr connectport [args-test.pl]
	Only start remote relay.
    remote.pl copy|splice localaddr remoteaddr remotessh [args-test.pl]
	Run test with local client and server.  Remote relay is
	started automatically with ssh on remotessh.
EOF
}

my $testfile;
our %args;
if (@ARGV and -f $ARGV[-1]) {
	$testfile = pop;
	do $testfile
	    or die "Do test file $testfile failed: ", $@ || $!;
}
my $mode =
	@ARGV == 3 && $ARGV[0] =~ /^\d+$/ && $ARGV[2] =~ /^\d+$/ ? "manual" :
	@ARGV == 4 && $ARGV[1] !~ /^\d+$/ && $ARGV[3] =~ /^\d+$/ ? "relay"  :
	@ARGV == 4 && $ARGV[1] !~ /^\d+$/ && $ARGV[3] !~ /^\d+$/ ? "auto"   :
	usage();

my $r;
if ($mode eq "relay") {
	$r = Relay->new(
	    forward		=> $ARGV[0],
	    logfile		=> dirname($0)."/remote.log",
	    func		=> \&relay,
	    %{$args{relay}},
	    listendomain	=> AF_INET,
	    listenaddr		=> $ARGV[1],
	    connectdomain	=> AF_INET,
	    connectaddr		=> $ARGV[2],
	    connectport		=> $ARGV[3],
	);
	open(my $log, '<', $r->{logfile})
	    or die "Remote log file open failed: $!";
	$SIG{__DIE__} = sub {
		die @_ if $^S;
		copy($log, \*STDERR);
		warn @_;
		exit 255;
	};
	copy($log, \*STDERR);
	$r->run;
	copy($log, \*STDERR);
	$r->up;
	copy($log, \*STDERR);
	$r->down;
	copy($log, \*STDERR);

	exit;
}

my $s = Server->new(
    func		=> \&read_stream,
    oobinline		=> 1,
    %{$args{server}},
    listendomain	=> AF_INET,
    listenaddr		=> ($mode eq "auto" ? $ARGV[1] : undef),
    listenport		=> ($mode eq "manual" ? $ARGV[0] : undef),
);
if ($mode eq "auto") {
	$r = Remote->new(
	    forward		=> $ARGV[0],
	    logfile		=> "relay.log",
	    testfile		=> $testfile,
	    %{$args{relay}},
	    remotessh		=> $ARGV[3],
	    listenaddr		=> $ARGV[2],
	    connectaddr		=> $ARGV[1],
	    connectport		=> $s->{listenport},
	);
	$r->run->up;
}
my $c = Client->new(
    func		=> \&write_stream,
    oobinline		=> 1,
    %{$args{client}},
    connectdomain	=> AF_INET,
    connectaddr		=> ($mode eq "manual" ? $ARGV[1] : $r->{listenaddr}),
    connectport		=> ($mode eq "manual" ? $ARGV[2] : $r->{listenport}),
);

$s->run;
$c->run->up;
$s->up;

$c->down;
$r->down if $r;
$s->down;

check_logs($c, $r, $s, %args);
