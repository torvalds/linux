#	$OpenBSD: Server.pm,v 1.15 2021/12/22 11:50:28 bluhm Exp $

# Copyright (c) 2010-2021 Alexander Bluhm <bluhm@openbsd.org>
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

package Server;
use parent 'Proc';
use Carp;
use Config;
use Socket qw(:DEFAULT IPPROTO_TCP TCP_NODELAY);
use Socket6;
use IO::Socket::IP;
use IO::Socket::SSL;

sub new {
	my $class = shift;
	my %args = @_;
	$args{logfile} ||= "server.log";
	$args{up} ||= "Accepted";
	my $self = Proc::new($class, %args);
	$self->{listendomain}
	    or croak "$class listen domain not given";
	$SSL_ERROR = "";
	my $iosocket = $self->{ssl} ? "IO::Socket::SSL" : "IO::Socket::IP";
	my $ls = $iosocket->new(
	    Proto		=> "tcp",
	    ReuseAddr		=> 1,
	    Domain		=> $self->{listendomain},
	    # IO::Socket::IP calls the domain family
	    Family		=> $self->{listendomain},
	    $self->{listenaddr} ? (LocalAddr => $self->{listenaddr}) : (),
	    $self->{listenport} ? (LocalPort => $self->{listenport}) : (),
	    SSL_server          => 1,
	    SSL_key_file	=> "server.key",
	    SSL_cert_file	=> "server.crt",
	    SSL_verify_mode	=> SSL_VERIFY_NONE,
	) or die ref($self), " $iosocket socket failed: $!,$SSL_ERROR";
	if ($self->{sndbuf}) {
		setsockopt($ls, SOL_SOCKET, SO_SNDBUF,
		    pack('i', $self->{sndbuf}))
		    or die ref($self), " set SO_SNDBUF failed: $!";
	}
	if ($self->{rcvbuf}) {
		setsockopt($ls, SOL_SOCKET, SO_RCVBUF,
		    pack('i', $self->{rcvbuf}))
		    or die ref($self), " set SO_RCVBUF failed: $!";
	}
	my $packstr = $Config{longsize} == 8 ? 'ql!' :
	    $Config{byteorder} == 1234 ? 'lxxxxl!xxxx' : 'xxxxll!';
	if ($self->{sndtimeo}) {
		setsockopt($ls, SOL_SOCKET, SO_SNDTIMEO,
		    pack($packstr, $self->{sndtimeo}, 0))
		    or die ref($self), " set SO_SNDTIMEO failed: $!";
	}
	if ($self->{rcvtimeo}) {
		setsockopt($ls, SOL_SOCKET, SO_RCVTIMEO,
		    pack($packstr, $self->{rcvtimeo}, 0))
		    or die ref($self), " set SO_RCVTIMEO failed: $!";
	}
	setsockopt($ls, IPPROTO_TCP, TCP_NODELAY, pack('i', 1))
	    or die ref($self), " set TCP_NODELAY failed: $!";
	listen($ls, 1)
	    or die ref($self), " socket listen failed: $!";
	my $log = $self->{log};
	print $log "listen sock: ",$ls->sockhost()," ",$ls->sockport(),"\n";
	$self->{listenaddr} = $ls->sockhost() unless $self->{listenaddr};
	$self->{listenport} = $ls->sockport() unless $self->{listenport};
	$self->{ls} = $ls;
	return $self;
}

sub child {
	my $self = shift;

	# in case we redo the accept, shutdown the old one
	shutdown(\*STDOUT, SHUT_WR);
	delete $self->{as};

	my $as = $self->{ls}->accept()
	    or die ref($self)," ",ref($self->{ls}),
	    " socket accept failed: $!,$SSL_ERROR";
	print STDERR "accept sock: ",$as->sockhost()," ",$as->sockport(),"\n";
	print STDERR "accept peer: ",$as->peerhost()," ",$as->peerport(),"\n";
	if ($self->{ssl}) {
		print STDERR "ssl version: ",$as->get_sslversion(),"\n";
		print STDERR "ssl cipher: ",$as->get_cipher(),"\n";
		print STDERR "ssl peer certificate:\n",
		    $as->dump_peer_certificate();
	}

	*STDIN = *STDOUT = $self->{as} = $as;
}

1;
