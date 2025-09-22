#!/usr/bin/perl
#	$OpenBSD: scapy.pl,v 1.5 2021/12/14 12:37:49 bluhm Exp $

# Copyright (c) 2010-2017 Alexander Bluhm <bluhm@openbsd.org>
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

use Relay;
use Remote;
require 'funcs.pl';

sub usage {
	die <<"EOF";
usage:
    scapy.pl localport remoteaddr remoteport scapy-test.py
	Run test with local client and server.  Remote relay
	forwarding from remoteaddr remoteport to server localport
	has to be started manually.
    scapy.pl copy|splice listenaddr connectaddr connectport
	Only start remote relay.
    scapy.pl copy|splice localaddr remoteaddr remotessh scapy-test.py
	Run test with local client and server.  Remote relay is
	started automatically with ssh on remotessh.
EOF
}

my $testfile;
if (@ARGV and -f $ARGV[-1]) {
	$testfile = pop;
	basename($testfile) =~ /^scapy-.*\.py$/
	    or die "Test file $testfile does not look like scapy script.\n";
}
my $mode =
	@ARGV == 3 && $ARGV[0] =~ /^\d+$/ && $ARGV[2] =~ /^\d+$/ ? "manual" :
	@ARGV == 4 && $ARGV[1] !~ /^\d+$/ && $ARGV[3] =~ /^\d+$/ ? "relay"  :
	@ARGV == 4 && $ARGV[1] !~ /^\d+$/ && $ARGV[3] !~ /^\d+$/ ? "auto"   :
	usage();
if (!$testfile and $mode =~ /manual|auto/) {
	usage();
}

my $r;
if ($mode eq "relay") {
	$r = Relay->new(
	    forward		=> $ARGV[0],
	    logfile		=> dirname($0)."/remote.log",
	    func		=> \&relay,
	    listendomain	=> AF_INET,
	    listenaddr		=> $ARGV[1],
	    connectdomain	=> AF_INET,
	    connectaddr		=> $ARGV[2],
	    connectport		=> $ARGV[3],
	    func		=> sub { errignore(@_); relay(@_); },
	    oobinline		=> 1,
	    rcvbuf		=> 2**12,
	    sndbuf		=> 2**12,
	    down		=> "Broken pipe|Connection reset by peer",
	    clientreadable	=> $testfile =~ /delay-connect/ ? 1 : 0,
	    connectnonblocking	=> 1,
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
	$r->loggrep(qr/^Spliced$/);
	copy($log, \*STDERR);
	$r->down;
	copy($log, \*STDERR);

	exit;
}

my $s = {
    listendomain        => AF_INET,
    listenaddr          => ($mode eq "auto" ? $ARGV[1] : undef),
    listenport          => ($mode eq "manual" ? $ARGV[0] : $$ & 0xffff),
};
if ($mode eq "auto") {
	$r = Remote->new(
	    forward		=> $ARGV[0],
	    logfile		=> "relay.log",
	    testfile		=> $testfile,
	    remotessh		=> $ARGV[3],
	    listenaddr		=> $ARGV[2],
	    connectaddr		=> $ARGV[1],
	    connectport		=> $s->{listenport},
	    down		=> "Broken pipe|Connection reset by peer",
	);
	$r->run->up;
}
my $c = {
    connectdomain       => AF_INET,
    connectaddr         => ($mode eq "manual" ? $ARGV[1] : $r->{listenaddr}),
    connectport         => ($mode eq "manual" ? $ARGV[2] : $r->{listenport}),
};

my @sudo = $ENV{SUDO} ? $ENV{SUDO} : ();
my @python = $ENV{PYTHON} ? split(' ', $ENV{PYTHON}) : ("python3");
my @cmd = (@sudo, @python, $testfile, $s->{listenport}, $c->{connectport});
system("@cmd")
    and die "Scapy script '@cmd' failed: $?";

$r->down if $r;
