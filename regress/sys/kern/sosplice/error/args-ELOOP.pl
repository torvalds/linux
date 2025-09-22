# test ELOOP for splicing loop

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";

our %args = (
    errno => 'ELOOP',
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
	) or die "socket connect failed: $!";

	my $ss = $sl->accept()
	    or die "socket splice accept failed: $!";

	$s->setsockopt(SOL_SOCKET, SO_SPLICE, pack('i', $ss->fileno()))
	    or die "splice failed: $!";

	defined($ss->send("foo\n"))
	    or die "socket splice send failed: $!";
	defined($s->recv(my $buf, 10))
	    or die "socket recv failed: $!";
	$buf eq "foo\n"
	    or die "socket recv unexpected content: $buf";
	defined($s->recv($buf, 10))
	    and die "socket recv succeeded";
    },
);
