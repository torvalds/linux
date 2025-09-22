#	$OpenBSD: Client.pm,v 1.6 2021/12/12 21:16:53 bluhm Exp $

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

package Client;
use parent 'Proc';
use Carp;
use Socket qw(IPPROTO_TCP TCP_NODELAY);
use Socket6;
use IO::Socket;
use IO::Socket::IP -register;
use constant SO_BINDANY => 0x1000;

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktracefile} ||= "client.ktrace";
	$args{logfile} ||= "client.log";
	$args{up} ||= "Connected";
	$args{down} ||= $args{alarm} ? "Alarm $class" :
	    "Shutdown $class|Broken pipe|Connection reset by peer";
	my $self = Proc::new($class, %args);
	$self->{domain}
	    or croak "$class domain not given";
	$self->{protocol}
	    or croak "$class protocol not given";
	$self->{connectaddr}
	    or croak "$class connect addr not given";
	$self->{connectport} || $self->{protocol} !~ /^(tcp|udp)$/
	    or croak "$class connect port not given";

	if ($self->{ktrace}) {
		unlink $self->{ktracefile};
		my @cmd = ("ktrace", "-f", $self->{ktracefile}, "-p", $$);
		do { local $> = 0; system(@cmd) }
		    and die ref($self), " system '@cmd' failed: $?";
	}

	my $cs;
	if ($self->{bindany}) {
		do { local $> = 0; $cs = IO::Socket->new(
		    Type	=> $self->{socktype},
		    Proto	=> $self->{protocol},
		    Domain	=> $self->{domain},
		) } or die ref($self), " socket connect failed: $!";
		do { local $> = 0; $cs->setsockopt(SOL_SOCKET, SO_BINDANY, 1) }
		    or die ref($self), " setsockopt SO_BINDANY failed: $!";
		my @rres = getaddrinfo($self->{bindaddr}, $self->{bindport}||0,
		    $self->{domain}, SOCK_STREAM, 0, AI_PASSIVE);
		$cs->bind($rres[3])
		    or die ref($self), " bind failed: $!";
	} elsif ($self->{bindaddr} || $self->{bindport}) {
		do { local $> = 0; $cs = IO::Socket->new(
		    Type	=> $self->{socktype},
		    Proto	=> $self->{protocol},
		    Domain	=> $self->{domain},
		    LocalAddr	=> $self->{bindaddr},
		    LocalPort	=> $self->{bindport},
		) } or die ref($self), " socket connect failed: $!";
	}
	if ($cs) {
		$self->{bindaddr} = $cs->sockhost();
		$self->{bindport} = $cs->sockport();
		$self->{cs} = $cs;
	}

	if ($self->{ktrace}) {
		my @cmd = ("ktrace", "-c", "-f", $self->{ktracefile}, "-p", $$);
		do { local $> = 0; system(@cmd) }
		    and die ref($self), " system '@cmd' failed: $?";
	}

	return $self;
}

sub child {
	my $self = shift;

	my $cs = $self->{cs} || do { local $> = 0; IO::Socket->new(
	    Type	=> $self->{socktype},
	    Proto	=> $self->{protocol},
	    Domain	=> $self->{domain},
	) } or die ref($self), " socket connect failed: $!";
	if ($self->{oobinline}) {
		setsockopt($cs, SOL_SOCKET, SO_OOBINLINE, pack('i', 1))
		    or die ref($self), " set oobinline connect failed: $!";
	}
	if ($self->{sndbuf}) {
		setsockopt($cs, SOL_SOCKET, SO_SNDBUF,
		    pack('i', $self->{sndbuf}))
		    or die ref($self), " set sndbuf connect failed: $!";
	}
	if ($self->{rcvbuf}) {
		setsockopt($cs, SOL_SOCKET, SO_RCVBUF,
		    pack('i', $self->{rcvbuf}))
		    or die ref($self), " set rcvbuf connect failed: $!";
	}
	if ($self->{protocol} eq "tcp") {
		setsockopt($cs, IPPROTO_TCP, TCP_NODELAY, pack('i', 1))
		    or die ref($self), " set nodelay connect failed: $!";
	}
	my @rres = getaddrinfo($self->{connectaddr}, $self->{connectport},
	    $self->{domain}, SOCK_STREAM);
	$cs->connect($rres[3])
	    or die ref($self), " connect failed: $!";
	print STDERR "connect sock: ",$cs->sockhost()," ",$cs->sockport(),"\n";
	print STDERR "connect peer: ",$cs->peerhost()," ",$cs->peerport(),"\n";
	$self->{bindaddr} = $cs->sockhost();
	$self->{bindport} = $cs->sockport();
	if ($self->{nonblocking}) {
		$cs->blocking(0)
		    or die ref($self), " set non-blocking connect failed: $!";
	}

	open(STDOUT, '>&', $cs)
	    or die ref($self), " dup STDOUT failed: $!";
	open(STDIN, '<&', $cs)
	    or die ref($self), " dup STDIN failed: $!";
}

1;
