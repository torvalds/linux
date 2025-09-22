# test that a slow (in this case sleeping) client causes relayd to slow down
# reading from the server (instead of balooning its buffers)

use strict;
use warnings;
use Errno ':POSIX';

my @errors = (EWOULDBLOCK);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

my $size = 2**21;

our %args = (
    client => {
	fast => 1,
	max => 100,
	func => sub {
	    my $self = shift;
	    http_request($self , $size, "1.0", "");
	    http_response($self , $size);
	    print STDERR "going to sleep\n";
	    ${$self->{server}}->loggrep(qr/blocked write/, 8)
		or die "no blocked write in server.log";
	    read_char($self, $size);
	    return;
	},
	rcvbuf => 2**12,
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    "tcp socket buffer 1024",
	    "match request header log",
	    "match request path log",
	],
    },
    server => {
	fast => 1,
	func => \&http_server,
	sndbuf => 2**12,
	sndtimeo => 2,
	loggrep => qr/blocked write .*: $errors/,

    },
    lengths => [$size],
);

1;
