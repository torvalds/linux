#!/usr/bin/perl
#	$OpenBSD: remote.pl,v 1.9 2016/08/25 22:56:13 bluhm Exp $

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
use Relayd;
use Server;
use Remote;
require 'funcs.pl';

sub usage {
	die <<"EOF";
usage:
    remote.pl localport remoteaddr remoteport [test-args.pl]
	Run test with local client and server.  Remote relayd
	forwarding from remoteaddr remoteport to server localport
	has to be started manually.
    remote.pl copy|splice listenaddr connectaddr connectport [test-args.pl]
	Only start remote relayd.
    remote.pl copy|splice localaddr remoteaddr remotessh [test-args.pl]
	Run test with local client and server.  Remote relayd is
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

my($s, $r, $c);
if ($mode eq "relay") {
	my($rport) = find_ports(num => 1);
	$r = Relayd->new(
	    forward             => $ARGV[0],
	    %{$args{relayd}},
	    listendomain        => AF_INET,
	    listenaddr          => $ARGV[1],
	    listenport          => $rport,
	    connectdomain       => AF_INET,
	    connectaddr         => $ARGV[2],
	    connectport         => $ARGV[3],
	    logfile             => dirname($0)."/remote.log",
	    conffile            => dirname($0)."/relayd.conf",
	    testfile            => $testfile,
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
	print STDERR "listen sock: $ARGV[1] $rport\n";
	<STDIN>;
	copy($log, \*STDERR);
	print STDERR "stdin closed\n";
	$r->kill_child;
	$r->down;
	copy($log, \*STDERR);

	exit;
}

my $redo = $args{lengths} && @{$args{lengths}};
$redo = 0 if $args{client}{http_vers};  # run only one persistent connection
$s = Server->new(
    forward             => $ARGV[0],
    func                => \&read_char,
    redo                => $redo,
    %{$args{server}},
    listendomain        => AF_INET,
    listenaddr          => ($mode eq "auto" ? $ARGV[1] : undef),
    listenport          => ($mode eq "manual" ? $ARGV[0] : undef),
    testfile            => $testfile,
    client              => \$c,
) unless $args{server}{noserver};
if ($mode eq "auto") {
	$r = Remote->new(
	    forward             => $ARGV[0],
	    logfile             => "relayd.log",
	    %{$args{relayd}},
	    remotessh           => $ARGV[3],
	    listenaddr          => $ARGV[2],
	    connectaddr         => $ARGV[1],
	    connectport         => $s ? $s->{listenport} : 1,
	    testfile            => $testfile,
	);
	$r->run->up;
}
$c = Client->new(
    forward             => $ARGV[0],
    func                => \&write_char,
    %{$args{client}},
    connectdomain       => AF_INET,
    connectaddr         => ($mode eq "manual" ? $ARGV[1] : $r->{listenaddr}),
    connectport         => ($mode eq "manual" ? $ARGV[2] : $r->{listenport}),
    testfile            => $testfile,
    server              => \$s,
) unless $args{client}{noclient};

$s->run unless $args{server}{noserver};
$c->run->up unless $args{client}{noclient};
$s->up unless $args{server}{noserver};

$c->down unless $args{client}{noclient};
$s->down unless $args{server}{noserver};
$r->close_child;
$r->down;

check_logs($c, $r, $s, %args);
