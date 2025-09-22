#	$OpenBSD: Server.pm,v 1.14 2021/12/22 15:14:13 bluhm Exp $

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
use Socket;
use Socket6;
use IO::Socket;
use IO::Socket::SSL;

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktracepid} = "ktrace" if $args{ktrace};
	$args{ktracepid} = $ENV{KTRACE} if $ENV{KTRACE};
	$args{ktracefile} ||= "server.ktrace";
	$args{logfile} ||= "server.log";
	$args{up} ||= "Accepted";
	my $self = Proc::new($class, %args);
	$self->{listenproto} ||= "udp";
	defined($self->{listendomain})
	    or croak "$class listen domain not given";
	return $self->listen();
}

sub listen {
	my $self = shift;
	$SSL_ERROR = "";
	my $iosocket = $self->{listenproto} eq "tls" ?
	    "IO::Socket::SSL" : "IO::Socket::IP";
	my $proto = $self->{listenproto};
	$proto = "tcp" if $proto eq "tls";
	my $ls = $iosocket->new(
	    Proto		=> $proto,
	    ReuseAddr		=> 1,
	    Domain		=> $self->{listendomain},
	    $self->{listenaddr}	? (LocalAddr => $self->{listenaddr}) : (),
	    $self->{listenport}	? (LocalPort => $self->{listenport}) : (),
	    SSL_server          => 1,
	    SSL_key_file	=> "server.key",
	    SSL_cert_file	=> "server.crt",
	    SSL_ca_file		=> ($self->{sslca} || "ca.crt"),
	    SSL_verify_mode     => ($self->{sslca} ?
		SSL_VERIFY_PEER : SSL_VERIFY_NONE),
	    $self->{sslca}	? (SSL_verifycn_scheme => "none") : (),
	    $self->{sslversion}	? (SSL_version => $self->{sslversion}) : (),
	    $self->{sslciphers}	? (SSL_cipher_list => $self->{sslciphers}) : (),
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
	if ($self->{listenproto} ne "udp") {
		listen($ls, 1)
		    or die ref($self), " socket listen failed: $!";
	}
	my $log = $self->{log};
	print $log "listen sock: ",$ls->sockhost()," ",$ls->sockport(),"\n";
	$self->{listenaddr} = $ls->sockhost() unless $self->{listenaddr};
	$self->{listenport} = $ls->sockport() unless $self->{listenport};
	$self->{ls} = $ls;
	return $self;
}

sub close {
	my $self = shift;
	$self->{ls}->close()
	    or die ref($self)," ",ref($self->{ls}),
	    " socket close failed: $!,$SSL_ERROR";
	delete $self->{ls};
	return $self;
}

sub run {
	my $self = shift;
	Proc::run($self, @_);
	return $self->close();
}

sub child {
	my $self = shift;

	# TLS 1.3 writes multiple messages without acknowledgement.
	# If the other side closes early, we want broken pipe error.
	$SIG{PIPE} = 'IGNORE' if $self->{listenproto} eq "tls";

	my $as = $self->{ls};
	if ($self->{listenproto} ne "udp") {
		$as = $self->{ls}->accept()
		    or die ref($self)," ",ref($self->{ls}),
		    " socket accept failed: $!,$SSL_ERROR";
		print STDERR "accept sock: ",$as->sockhost()," ",
		    $as->sockport(),"\n";
		print STDERR "accept peer: ",$as->peerhost()," ",
		    $as->peerport(),"\n";
	}
	if ($self->{listenproto} eq "tls") {
		print STDERR "ssl version: ",$as->get_sslversion(),"\n";
		print STDERR "ssl cipher: ",$as->get_cipher(),"\n";
		print STDERR "ssl subject: ", $as->peer_certificate("subject")
		    ,"\n" if $self->{sslca};
	}

	*STDIN = *STDOUT = $self->{as} = $as;
}

1;
