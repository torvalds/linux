# test EPROTONOSUPPORT for splicing tcp to udp socket

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'EPROTONOSUPPORT',
    func => sub {
	my $s = IO::Socket::IP->new(
	    Proto => "udp",
	) or die "socket failed: $!";

	my $ss = IO::Socket::IP->new(
	    Proto => "tcp",
	) or die "socket splice failed: $!";

	$s->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $ss->fileno()))
	    and die "splice udp to tcp socket succeeded";
    },
);
