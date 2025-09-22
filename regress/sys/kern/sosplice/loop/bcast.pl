#!/usr/bin/perl
#	$OpenBSD: bcast.pl,v 1.2 2025/03/13 10:05:14 yasuoka Exp $

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
getopts('b:v', \%opts) or do {
    print STDERR <<"EOF";
usage: $0 [-v] [-b bcast]
    -b bcast	broadcast address, default 255.255.255.255
    -v		verbose
EOF
    exit(2);
};

my $broadcast = $opts{b} || "255.255.255.255";
my $verbose = $opts{v};

my $timeout = 10;
$SIG{ALRM} = sub { die "Timeout triggered after $timeout seconds" };
alarm($timeout);

my $ls = IO::Socket::IP->new(
    Broadcast		=> $opts{b} ? 1 : undef,
    GetAddrInfoFlags	=> AI_PASSIVE,
    LocalHost		=> $broadcast,
    Proto		=> "udp",
    Type		=> SOCK_DGRAM,
) or die "Listen socket failed: $@";
my ($host, $service) = $ls->sockhost_service(1);
print "listen on host '$host' service '$service'\n" if $verbose;

my $cs = IO::Socket::IP->new(
    PeerHost		=> $host,
    PeerService		=> $service,
    Proto		=> "udp",
    Type		=> SOCK_DGRAM,
) or die "Connect socket failed: $@";
$cs->setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
  or die "Set socket option SO_BROADCAST failed: $!";
print "connect to host '$host' service '$service'\n" if $verbose;

my $as = $ls;
my $peer = $cs->sockname();
$as->connect($peer)
    or die "Connect passive socket failed: $!";
if ($verbose) {
    my ($err, $peerhost, $peerservice) = getnameinfo($peer,
	NI_NUMERICHOST | NI_NUMERICSERV);
    $err and die "Getnameinfo failed: $err";
    print "accept from host '$peerhost' service '$peerservice'\n";
}

setsplice($as, $cs)
    or die "Splice accept to connect socket failed: $!";
setsplice($cs, $as)
    or die "Splice connect to accept socket failed: $!";

system("\${SUDO} fstat -n -p $$") if $verbose;
my ($msg, $buf) = "foo";
$cs->send($msg, 0)
    or die "Send to connect socket failed: $!";
defined $as->recv($buf, 100, 0)
    or die "Recv from accept socket failed: $!";
$msg eq $buf
    or die "Value modified in splice chain";
$! = geterror($as)
    or die "No error at accept socket";
$!{ELOOP}
    or die "Errno at accept socket is not ELOOP: $!";

# addresses are asymmetric, try it the other way around
$msg = "bar";
$as->send($msg, 0)
    or die "Send to accept socket failed: $!";
defined $cs->recv($buf, 100, 0)
    or die "Recv from connect socket failed: $!";
$msg eq $buf
    or die "Value modified in splice chain";
$! = geterror($cs)
    or die "No error at connect socket";
$!{ELOOP}
    or die "Errno at connect socket is not ELOOP: $!";
