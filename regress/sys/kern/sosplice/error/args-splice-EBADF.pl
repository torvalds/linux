# test EBADF for splicing with non existing fileno

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'EBADF',
    func => sub {
	my $sl = IO::Socket::IP->new(
	    Proto => "tcp",
	    Listen => 5,
	    LocalAddr => "127.0.0.1",
	) or die "socket listen failed: $!";

	my $s = IO::Socket::IP->new(
	    Proto => "tcp",
	    PeerAddr => $sl->sockhost(),
	    PeerPort => $sl->sockport(),
	) or die "socket failed: $!";

	$s->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', 23))
	    and die "splice with non existing fileno succeeded";
    },
);
