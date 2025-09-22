# test ENOTCONN for splicing to unconnected udp socket

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'ENOTCONN',
    func => sub {
	my $sb = IO::Socket::IP->new(
	    Proto => "udp",
	    LocalAddr => "127.0.0.1",
	) or die "socket bind failed: $!";

	my $sc = IO::Socket::IP->new(
	    Proto => "udp",
	    PeerAddr => $sb->sockhost(),
	    PeerPort => $sb->sockport(),
	) or die "socket connect failed: $!";

	$sb->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $sc->fileno()))
	    or die "splice from unconnected socket failed: $!";
	$sc->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $sb->fileno()))
	    and die "splice to unconnected socket succeeded";
    },
);
