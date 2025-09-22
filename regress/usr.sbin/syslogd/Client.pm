#	$OpenBSD: Client.pm,v 1.16 2021/12/22 15:14:13 bluhm Exp $

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
use Socket;
use Socket6;
use IO::Socket;
use IO::Socket::SSL;
use Sys::Syslog qw(:standard :extended :macros);

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktracepid} = "ktrace" if $args{ktrace};
	$args{ktracepid} = $ENV{KTRACE} if $ENV{KTRACE};
	$args{ktracefile} ||= "client.ktrace";
	$args{logfile} ||= "client.log";
	$args{up} ||= "Openlog";
	my $self = Proc::new($class, %args);
	if (defined($self->{connectdomain})) {
		$self->{connectproto} ||= "udp";
	}
	return $self;
}

sub child {
	my $self = shift;

	if ($self->{early}) {
		my @sudo = $ENV{SUDO} ? $ENV{SUDO} : "env";
		my @flush = (@sudo, "./logflush");
		system(@flush);
	}

	# TLS 1.3 writes multiple messages without acknowledgement.
	# If the other side closes early, we want broken pipe error.
	$SIG{PIPE} = 'IGNORE' if defined($self->{connectdomain}) &&
	    $self->{connectproto} eq "tls";

	if (defined($self->{connectdomain}) &&
	    $self->{connectdomain} ne "sendsyslog") {
		my $cs;
		if ($self->{connectdomain} == AF_UNIX) {
			$cs = IO::Socket::UNIX->new(
			    Type => SOCK_DGRAM,
			    Peer => $self->{connectpath} || "/dev/log",
			) or die ref($self), " socket unix failed: $!";
			$cs->setsockopt(SOL_SOCKET, SO_SNDBUF, 10000)
			    or die ref($self), " setsockopt failed: $!";
		} else {
			$SSL_ERROR = "";
			my $iosocket = $self->{connectproto} eq "tls" ?
			    "IO::Socket::SSL" : "IO::Socket::IP";
			my $proto = $self->{connectproto};
			$proto = "tcp" if $proto eq "tls";
			$cs = $iosocket->new(
			    Proto               => $proto,
			    Domain              => $self->{connectdomain},
			    PeerAddr            => $self->{connectaddr},
			    PeerPort            => $self->{connectport},
			    $self->{sslcert} ?
				(SSL_cert_file => $self->{sslcert}) : (),
			    $self->{sslkey} ?
				(SSL_key_file => $self->{sslkey}) : (),
			    $self->{sslca} ?
				(SSL_ca_file => $self->{sslca}) : (),
			    SSL_verify_mode     => ($self->{sslca} ?
				SSL_VERIFY_PEER : SSL_VERIFY_NONE),
			    $self->{sslversion} ?
				(SSL_version => $self->{sslversion}) : (),
			    $self->{sslciphers} ?
				(SSL_cipher_list => $self->{sslciphers}) : (),
			) or die ref($self), " $iosocket socket connect ".
			    "failed: $!,$SSL_ERROR";
			if ($self->{sndbuf}) {
				setsockopt($cs, SOL_SOCKET, SO_SNDBUF,
				    pack('i', $self->{sndbuf})) or die
				    ref($self), " set SO_SNDBUF failed: $!";
			}
			if ($self->{rcvbuf}) {
				setsockopt($cs, SOL_SOCKET, SO_RCVBUF,
				    pack('i', $self->{rcvbuf})) or die
				    ref($self), " set SO_RCVBUF failed: $!";
			}
			print STDERR "connect sock: ",$cs->sockhost()," ",
			    $cs->sockport(),"\n";
			print STDERR "connect peer: ",$cs->peerhost()," ",
			    $cs->peerport(),"\n";
			if ($self->{connectproto} eq "tls") {
				print STDERR "ssl version: ",
				    $cs->get_sslversion(),"\n";
				print STDERR "ssl cipher: ",
				    $cs->get_cipher(),"\n";
				print STDERR "ssl issuer: ",
				    $cs->peer_certificate('issuer'),"\n";
				print STDERR "ssl subject: ",
				    $cs->peer_certificate('subject'),"\n";
				print STDERR "ssl cn: ",
				    $cs->peer_certificate('cn'),"\n";
			}
		}

		IO::Handle::flush(\*STDOUT);
		*STDIN = *STDOUT = $self->{cs} = $cs;
		select(STDOUT);
	}

	if ($self->{logsock}) {
		setlogsock($self->{logsock})
		    or die ref($self), " setlogsock failed: $!";
	}
	# we take LOG_UUCP as it is not used nowadays
	openlog("syslogd-regress", "perror,pid", LOG_UUCP);
}

1;
