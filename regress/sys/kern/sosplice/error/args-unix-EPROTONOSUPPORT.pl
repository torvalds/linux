# test EPROTONOSUPPORT for splicing unix sockets

use strict;
use warnings;
use IO::Socket::UNIX;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'EPROTONOSUPPORT',
    func => sub {
	my $s = IO::Socket::UNIX->new(
	    Type => SOCK_STREAM,
	) or die "socket failed: $!";

	my $ss = IO::Socket::UNIX->new(
	    Type => SOCK_STREAM,
	) or die "socket splice failed: $!";

	$s->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $ss->fileno()))
	    and die "splice unix sockets succeeded";
    },
);
