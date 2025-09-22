# test EOPNOTSUPP for splicing from listen socket

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'EOPNOTSUPP',
    func => sub {
	my $s = IO::Socket::IP->new(
	    Proto => "tcp",
	    Listen => 1,
	) or die "socket failed: $!";

	my $ss = IO::Socket::IP->new(
	    Proto => "tcp",
	) or die "socket splice failed: $!";

	$s->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $ss->fileno()))
	    and die "splice from listen socket succeeded";
    },
);
