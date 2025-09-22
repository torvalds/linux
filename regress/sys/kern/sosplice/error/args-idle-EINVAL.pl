# test EINVAL for splicing with negative idle timeout

use strict;
use warnings;
use IO::Socket::IP;
use BSD::Socket::Splice "SO_SPLICE";
use Config;

our %args = (
    errno => 'EINVAL',
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

	my $ss = IO::Socket::IP->new(
	    Proto => "tcp",
	    PeerAddr => $sl->sockhost(),
	    PeerPort => $sl->sockport(),
	) or die "socket splice failed: $!";

	my $packed;
	if ($Config{longsize} == 8) {
	    $packed = pack('ixxxxqql!', $ss->fileno(),-1,-1-1);
	} else {
	    my $pad = $Config{ARCH} eq 'i386'? '': 'xxxx';
	    my $packstr = "i${pad}lllll!${pad}";
	    $packed = pack($packstr, $ss->fileno(),0,0,-1,-1,-1);
	}
	$s->setsockopt(SOL_SOCKET, SO_SPLICE, $packed)
	    and die "splice with negative idle timeout succeeded";
    },
);
