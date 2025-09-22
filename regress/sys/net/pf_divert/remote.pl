#!/usr/bin/perl
#	$OpenBSD: remote.pl,v 1.10 2024/06/08 22:50:40 bluhm Exp $

# Copyright (c) 2010-2024 Alexander Bluhm <bluhm@openbsd.org>
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

BEGIN {
	if ($> == 0 && $ENV{SUDO_UID}) {
		$> = $ENV{SUDO_UID};
	}
}

use File::Basename;
use File::Copy;
use Getopt::Std;
use Socket;
use Socket6;

use Client;
use Server;
use Remote;
use Packet;
require 'funcs.pl';

sub usage {
	die <<"EOF";
usage:
    remote.pl af bindaddr connectaddr connectport test-args.pl
	Only start remote relay.
    remote.pl af bindaddr connectaddr connectport bindport test-args.pl
	Only start remote relay with fixed port, needed for reuse.
    remote.pl af localaddr fakeaddr remotessh test-args.pl
	Run test with local client and server.  Remote relay is
	started automatically with ssh on remotessh.
    remote.pl af localaddr fakeaddr remotessh clientport serverport test-args.pl
	Run test with local client and server and fixed port, needed for reuse.
    -f	flush regress states
EOF
}

my $command = "$0 @ARGV";
my $test;
our %args;
if (@ARGV) {
	$test = pop;
	do $test
	    or die "Do test file $test failed: ", $@ || $!;
}
my %opts;
getopts("f", \%opts) or usage();
my($af, $domain, $protocol);
if (@ARGV) {
	$af = shift;
	$domain =
	    $af eq "inet" ? AF_INET :
	    $af eq "inet6" ? AF_INET6 :
	    die "address family must be 'inet' or 'inet6\n";
	$protocol = $args{protocol};
	$protocol = $protocol->({ %args, af => $af, domain => $domain, })
	    if ref $protocol eq 'CODE';
}
my $mode =
	@ARGV >= 3 && $ARGV[0] !~ /^\d+$/ && $ARGV[2] =~ /^\d+$/ ? "divert" :
	@ARGV >= 3 && $ARGV[0] !~ /^\d+$/ && $ARGV[2] !~ /^\d+$/ ? "auto"   :
	usage();
my($clientport, $serverport, $bindport);
if (@ARGV == 5 && $mode eq "auto") {
	($clientport, $serverport) = @ARGV[3,4];
} elsif (@ARGV == 4 && $mode eq "divert") {
	($bindport) = $ARGV[3];
} elsif (@ARGV != 3) {
	usage();
}

my $divert = $args{divert};
my ($local, $remote) = ("client", "server");
($local, $remote) = ($remote, $local) if $mode eq "divert";
($local, $remote) = ($remote, $local) if $divert =~ /reply|out/;
my ($srcaddr, $dstaddr)	= @ARGV[0,1];
($srcaddr, $dstaddr) = ($dstaddr, $srcaddr) if $mode eq "divert";
($srcaddr, $dstaddr) = ($dstaddr, $srcaddr) if $divert =~ /reply|out/;

my ($logfile, $ktracefile, $packetlog, $packetktrace);
if ($mode eq "divert") {
	$logfile	= dirname($0)."/remote.log";
	$ktracefile	= dirname($0)."/remote.ktrace";
	$packetlog	= dirname($0)."/packet.log";
	$packetktrace	= dirname($0)."/packet.ktrace";
}

my ($c, $l, $r, $s);
if ($local eq "server") {
	$l = $s = Server->new(
	    ktrace		=> $ENV{KTRACE},
	    %args,
	    %{$args{server}},
	    logfile		=> $logfile,
	    ktracefile		=> $ktracefile,
	    af			=> $af,
	    domain		=> $domain,
	    protocol		=> $protocol,
	    listenaddr		=>
		$mode ne "divert" || $divert =~ /packet/ ? $ARGV[0] :
		$af eq "inet" ? "127.0.0.1" : "::1",
	    listenport		=> $serverport || $bindport,
	    srcaddr		=> $srcaddr,
	    dstaddr		=> $dstaddr,
	) if $args{server};
}
if ($mode eq "auto") {
	$r = Remote->new(
	    %args,
	    opts		=> \%opts,
	    down		=> $args{packet} && "Shutdown Packet",
	    logfile		=> "$remote.log",
	    ktracefile		=> "$remote.ktrace",
	    testfile		=> $test,
	    af			=> $af,
	    remotessh		=> $ARGV[2],
	    bindaddr		=> $ARGV[1],
	    bindport		=> $remote eq "client" ?
		$clientport : $serverport,
	    connect		=> $remote eq "client",
	    connectaddr		=> $ARGV[0],
	    connectport		=> $s ? $s->{listenport} : 0,
	);
	$r->run->up;
	$r->loggrep(qr/^Diverted$/, 10)
	    or die "no Diverted in $r->{logfile}";
}
if ($local eq "client") {
	$l = $c = Client->new(
	    ktrace		=> $ENV{KTRACE},
	    %args,
	    %{$args{client}},
	    logfile		=> $logfile,
	    ktracefile		=> $ktracefile,
	    af			=> $af,
	    domain		=> $domain,
	    protocol		=> $protocol,
	    connectaddr		=> $ARGV[1],
	    connectport		=> $r ? $r->{listenport} : $ARGV[2],
	    bindany		=> $mode eq "divert",
	    bindaddr		=> $ARGV[0],
	    bindport		=> $clientport || $bindport,
	    srcaddr		=> $srcaddr,
	    dstaddr		=> $dstaddr,
	) if $args{client};
}
$l->{log}->print("local command: $command\n") if $l;

if ($mode eq "divert") {
	open(my $log, '<', $l->{logfile})
	    or die "Remote log file open failed: $!";
	$SIG{__DIE__} = sub {
		die @_ if $^S;
		copy($log, \*STDERR);
		warn @_;
		exit 255;
	};
	copy($log, \*STDERR);

	my ($p, $plog);
	$p = Packet->new(
	    ktrace		=> $ENV{KTRACE},
	    %args,
	    %{$args{packet}},
	    logfile		=> $packetlog,
	    ktracefile		=> $packetktrace,
	    af			=> $af,
	    domain		=> $domain,
	    bindport		=> 666,
	) if $args{packet};

	if ($p) {
		open($plog, '<', $p->{logfile})
		    or die "Remote packet log file open failed: $!";
		$SIG{__DIE__} = sub {
			die @_ if $^S;
			copy($log, \*STDERR);
			copy_prefix(ref $p, $plog, \*STDERR);
			warn @_;
			exit 255;
		};
		copy_prefix(ref $p, $plog, \*STDERR);
		$p->run;
		copy_prefix(ref $p, $plog, \*STDERR);
		$p->up;
		copy_prefix(ref $p, $plog, \*STDERR);
	}

	my @cmd = qw(pfctl -a regress -f -);
	my $pf;
	do { local $> = 0; open($pf, '|-', @cmd) }
	    or die "Open pipe to pf '@cmd' failed: $!";
	if ($local eq "server") {
		my $port = $protocol =~ /^(tcp|udp)$/ ?
		    "port $s->{listenport}" : "";
		my $divertport = $port || "port 1";  # XXX bad pf syntax
		my $divertcommand = $divert =~ /packet/ ?
		    "divert-packet port 666" :
		    "divert-to $s->{listenaddr} $divertport";
		print $pf "pass in log $af proto $protocol ".
		    "from $ARGV[1] to $ARGV[0] $port $divertcommand ".
		    "label regress\n";
	}
	if ($local eq "client") {
		my $port = $protocol =~ /^(tcp|udp)$/ ?
		    "port $ARGV[2]" : "";
		my $divertcommand = $divert =~ /packet/ ?
		    "divert-packet port 666" : "divert-reply";
		print $pf "pass out log $af proto $protocol ".
		    "from $c->{bindaddr} to $ARGV[1] $port $divertcommand ".
		    "label regress\n";
	}
	close($pf) or die $! ?
	    "Close pipe to pf '@cmd' failed: $!" :
	    "pf '@cmd' failed: $?";
	if ($opts{f}) {
		@cmd = qw(pfctl -k label -k regress);
		do { local $> = 0; system(@cmd) }
		    and die "Execute '@cmd' failed: $!";
	}
	print STDERR "Diverted\n";

	$l->run;
	copy($log, \*STDERR);
	$l->up;
	copy($log, \*STDERR);
	$l->down;
	copy($log, \*STDERR);

	if ($p) {
		copy_prefix(ref $p, $plog, \*STDERR);
		$p->down;
		copy_prefix(ref $p, $plog, \*STDERR);
	}

	exit;
}

$s->run if $s;
$c->run->up if $c;
$s->up if $s;

$c->down if $c;
# remote side has 20 seconds timeout, wait longer than that here
$r->down(30) if $r;
$s->down if $s;

check_logs($c || $r, $r, $s || $r, %args);

sub copy_prefix {
	my ($prefix, $src, $dst) = @_;

	local $_;
	while (defined($_ = <$src>)) {
		chomp;
		print $dst "$prefix: $_\n" if length;
	}
	$src->clearerr();
}
