#	$OpenBSD: Relay.pm,v 1.4 2021/12/14 12:37:49 bluhm Exp $

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

package Relay;
use parent 'Proc';
use Carp;
use Errno 'EINPROGRESS';
use Socket qw(IPPROTO_TCP TCP_NODELAY);
use Socket6;
use IO::Socket;
use IO::Socket::IP -register;

sub new {
	my $class = shift;
	my %args = @_;
	$args{logfile} ||= "relay.log";
	$args{up} ||= "Connected";
	$args{forward}
	    or croak "$class forward not given";
	my $self = Proc::new($class, %args);
	$self->{protocol} ||= "tcp";
	$self->{listendomain}
	    or croak "$class listen domain not given";
	$self->{connectdomain}
	    or croak "$class connect domain not given";
	$self->{connectaddr}
	    or croak "$class connect addr not given";
	$self->{connectport}
	    or croak "$class connect port not given";
	my $ls = IO::Socket->new(
	    Proto	=> $self->{protocol},
	    ReuseAddr	=> 1,
	    Domain	=> $self->{listendomain},
	    $self->{listenaddr} ? (LocalAddr => $self->{listenaddr}) : (),
	    $self->{listenport} ? (LocalPort => $self->{listenport}) : (),
	) or die ref($self), " socket failed: $!";
	if ($self->{oobinline}) {
		setsockopt($ls, SOL_SOCKET, SO_OOBINLINE, pack('i', 1))
		    or die ref($self), " set oobinline listen failed: $!";
	}
	if ($self->{sndbuf}) {
		setsockopt($ls, SOL_SOCKET, SO_SNDBUF,
		    pack('i', $self->{sndbuf}))
		    or die ref($self), " set sndbuf listen failed: $!";
	}
	if ($self->{rcvbuf}) {
		setsockopt($ls, SOL_SOCKET, SO_RCVBUF,
		    pack('i', $self->{rcvbuf}))
		    or die ref($self), " set rcvbuf listen failed: $!";
	}
	if ($self->{protocol} eq "tcp") {
	setsockopt($ls, IPPROTO_TCP, TCP_NODELAY, pack('i', 1))
	    or die ref($self), " set nodelay listen failed: $!";
	listen($ls, 1)
	    or die ref($self), " listen failed: $!";
	}
	my $log = $self->{log};
	print $log "listen sock: ",$ls->sockhost()," ",$ls->sockport(),"\n";
	$self->{listenaddr} = $ls->sockhost() unless $self->{listenaddr};
	$self->{listenport} = $ls->sockport() unless $self->{listenport};
	$self->{ls} = $ls;
	return $self;
}

sub child {
	my $self = shift;

	my $as = $self->{ls};
	if ($self->{protocol} eq "tcp") {
		$as = $self->{ls}->accept()
		    or die ref($self), " socket accept failed: $!";
		print STDERR "accept sock: ",$as->sockhost()," ",
		    $as->sockport(),"\n";
		print STDERR "accept peer: ",$as->peerhost()," ",
		    $as->peerport(),"\n";
	}
	if ($self->{nonblocking}) {
		$as->blocking(0)
		    or die ref($self), " set non-blocking accept failed: $!";
	}

	open(STDIN, '<&', $as)
	    or die ref($self), " dup STDIN failed: $!";
	print STDERR "Accepted\n";

	if ($self->{clientreadable}) {
		my $idle = 15;  # timeout
		my $rin = '';
		vec($rin, fileno($as), 1) = 1;
		defined(my $n = select($rin, undef, undef, $idle))
		    or die ref($self), " select failed: $!";
		$idle && $n == 0
		    and die ref($self), " select timeout";
	}

	my $cs = IO::Socket->new(
	    Proto	=> $self->{protocol},
	    Domain	=> $self->{connectdomain},
	) or die ref($self), " socket connect failed: $!";
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
	if ($self->{connectnonblocking}) {
		$cs->blocking(0)
		    or die ref($self), " set non-blocking connect failed: $!";
	}
	my @rres = getaddrinfo($self->{connectaddr}, $self->{connectport},
	    $self->{connectdomain}, SOCK_STREAM);
	$cs->connect($rres[3]) || $!{EINPROGRESS}
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
}

1;
