#	$OpenBSD: Client.pm,v 1.15 2024/10/28 19:57:02 tb Exp $

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

package Client;
use parent 'Proc';
use Carp;
use Socket qw(:DEFAULT IPPROTO_TCP TCP_NODELAY);
use Socket6;
use IO::Socket::IP;
use IO::Socket::SSL;

sub new {
	my $class = shift;
	my %args = @_;
	$args{logfile} ||= "client.log";
	$args{up} ||= "Connected";
	$args{timefile} //= "time.log";
	my $self = Proc::new($class, %args);
	$self->{connectdomain}
	    or croak "$class connect domain not given";
	$self->{connectaddr}
	    or croak "$class connect addr not given";
	$self->{connectport}
	    or croak "$class connect port not given";
	return $self;
}

sub child {
	my $self = shift;

	# in case we redo the connect, shutdown the old one
	shutdown(\*STDOUT, SHUT_WR);
	delete $self->{cs};

	$SSL_ERROR = "";
	my $iosocket = $self->{ssl} ? "IO::Socket::SSL" : "IO::Socket::IP";
	my $cs = $iosocket->new(
	    Proto		=> "tcp",
	    Domain		=> $self->{connectdomain},
	    # IO::Socket::IP calls the domain family
	    Family		=> $self->{connectdomain},
	    PeerAddr		=> $self->{connectaddr},
	    PeerPort		=> $self->{connectport},
	    SSL_verify_mode	=> SSL_VERIFY_NONE,
	    SSL_use_cert	=> $self->{offertlscert} ? 1 : 0,
	    SSL_cert_file	=> $self->{offertlscert} ?
	                               "client.crt" : "",
	    SSL_key_file	=> $self->{offertlscert} ?
	                               "client.key" : "",
	) or die ref($self), " $iosocket socket connect failed: $!,$SSL_ERROR";
	if ($self->{sndbuf}) {
		setsockopt($cs, SOL_SOCKET, SO_SNDBUF,
		    pack('i', $self->{sndbuf}))
		    or die ref($self), " set SO_SNDBUF failed: $!";
	}
	if ($self->{rcvbuf}) {
		setsockopt($cs, SOL_SOCKET, SO_RCVBUF,
		    pack('i', $self->{rcvbuf}))
		    or die ref($self), " set SO_SNDBUF failed: $!";
	}
	if ($self->{sndtimeo}) {
		setsockopt($cs, SOL_SOCKET, SO_SNDTIMEO,
		    pack('l!l!', $self->{sndtimeo}, 0))
		    or die ref($self), " set SO_SNDTIMEO failed: $!";
	}
	if ($self->{rcvtimeo}) {
		setsockopt($cs, SOL_SOCKET, SO_RCVTIMEO,
		    pack('l!l!', $self->{rcvtimeo}, 0))
		    or die ref($self), " set SO_RCVTIMEO failed: $!";
	}
	setsockopt($cs, IPPROTO_TCP, TCP_NODELAY, pack('i', 1))
	    or die ref($self), " set TCP_NODELAY failed: $!";

	print STDERR "connect sock: ",$cs->sockhost()," ",$cs->sockport(),"\n";
	print STDERR "connect peer: ",$cs->peerhost()," ",$cs->peerport(),"\n";
	if ($self->{ssl}) {
		print STDERR "ssl version: ",$cs->get_sslversion(),"\n";
		print STDERR "ssl cipher: ",$cs->get_cipher(),"\n";
		print STDERR "ssl peer certificate:\n",
		    $cs->dump_peer_certificate();

		if ($self->{offertlscert}) {
			print STDERR "ssl client certificate:\n";
			print STDERR "Subject Name: ",
				"${\$cs->sock_certificate('subject')}\n";
			print STDERR "Issuer  Name: ",
				"${\$cs->sock_certificate('issuer')}\n";
		}
	}

	*STDIN = *STDOUT = $self->{cs} = $cs;
}

1;
