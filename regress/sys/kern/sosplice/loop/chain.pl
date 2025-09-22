#!/usr/bin/perl
#	$OpenBSD: chain.pl,v 1.1 2021/01/02 01:27:45 bluhm Exp $

# Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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
use BSD::Socket::Splice qw(setsplice geterror);
use Errno;
use Getopt::Std;
use IO::Socket::IP;
use Socket qw(getnameinfo AI_PASSIVE NI_NUMERICHOST NI_NUMERICSERV);

# from /usr/include/sys/mbuf.h
use constant M_MAXLOOP => 128;

my %opts;
getopts('46c:p:v', \%opts) or do {
    print STDERR <<"EOF";
usage: $0 [-46v] [-c count] [-p proto]
    -4		use IPv4
    -6		use IPv6
    -c count	number of splices in the chain, default 1
    -p proto	protocol, tcp or udp, default tcp
    -v		verbose
EOF
    exit(2);
};

$opts{4} && $opts{6}
    and die "Cannot use -4 and -6 together";
my $localhost = $opts{4} ? "127.0.0.1" : $opts{6} ? "::1" : "localhost";
my $count = $opts{c} || 1;
my $proto = $opts{p} || "tcp";
my $type = $proto eq "tcp" ? SOCK_STREAM : SOCK_DGRAM;
my $verbose = $opts{v};

my $timeout = 10;
$SIG{ALRM} = sub { die "Timeout triggered after $timeout seconds" };
alarm($timeout);

my $ls = IO::Socket::IP->new(
    GetAddrInfoFlags	=> AI_PASSIVE,
    Listen		=> ($type == SOCK_STREAM) ? 1 : undef,
    LocalHost		=> $localhost,
    Proto		=> $proto,
    Type		=> $type,
) or die "Listen socket failed: $@";
my ($host, $service) = $ls->sockhost_service(1);
print "listen on host '$host' service '$service'\n" if $verbose;

my (@cs, @as);
foreach (0..$count) {
    my $cs = IO::Socket::IP->new(
	PeerHost	=> $host,
	PeerService	=> $service,
	Proto		=> $proto,
	Type		=> $type,
    ) or die "Connect socket failed: $@";
    print "connect to host '$host' service '$service'\n" if $verbose;

    my $as;
    if ($type == SOCK_STREAM) {
	($as, my $peer) = $ls->accept()
	    or die "Accept socket failed: $!";
	if ($verbose) {
	    my ($err, $peerhost, $peerservice) = getnameinfo($peer,
		NI_NUMERICHOST | NI_NUMERICSERV);
	    $err and die "Getnameinfo failed: $err";
	    print "accept from host '$peerhost' service '$peerservice'\n";
	}
    } else {
	$as = $ls;
	$ls = IO::Socket::IP->new(
	    GetAddrInfoFlags	=> AI_PASSIVE,
	    LocalHost		=> $localhost,
	    Proto		=> $proto,
	    Type		=> $type,
	) or die "Listen socket failed: $@";
	($host, $service) = $ls->sockhost_service(1);
    }
    if (@as) {
	setsplice($as[-1], $cs)
	    or die "Splice previous accept to connect socket failed: $!";
    }
    push @cs, $cs;
    push @as, $as;
}

system("\${SUDO} fstat -n -p $$") if $verbose;
my ($msg, $buf) = "foo";
$cs[0]->send($msg, 0)
    or die "Send to first connect socket failed: $!";
if ($count > M_MAXLOOP) {
    $as[-1]->blocking(0);
    sleep 1;
    defined $as[-1]->recv($buf, 100, 0)
	and die "Recv from last accept socket succeeded: $buf";
    $!{EWOULDBLOCK}
	or die "Recv errno is not EWOULDBLOCK: $!";
    # reaching maxloop unsplices, message stays in socket
    $#as = M_MAXLOOP;
    $! = geterror($as[-1])
	or die "No error at maxloop accept socket";
    $!{ELOOP}
	or die "Errno at maxloop accept socket is not ELOOP: $!";
}
defined $as[-1]->recv($buf, 100, 0)
    or die "Recv from last accept socket failed: $!";
$msg eq $buf
    or die "Value modified in splice chain";
