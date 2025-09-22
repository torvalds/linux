# test waiting for splice finish with read and eof has happend

use strict;
use warnings;

our %args = (
    client => {
	# fill server buffer, relay send buffer, half relay recv buffer
	# then send eof
	len => 2**13 + 2**10,
    },
    relay => {
	nonblocking => 1,
	readblocking => 1,
	rcvbuf => 2**12,
	sndbuf => 2**12,
    },
    server => {
	# wait until all buffers are filled and client sends eof
	func => sub { sleep 4; read_stream(@_); },
	rcvbuf => 2**12,
    },
    len => 9216,
    md5 => "6d263239be35ccf30cb04c5f58a35dbe",
);
