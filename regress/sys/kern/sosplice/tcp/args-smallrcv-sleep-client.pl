# test with smaller relay receive buffer delay before client

use strict;
use warnings;

our %args = (
    client => {
	func => sub { sleep 3; write_stream(@_); },
	len => 2**17,
    },
    relay => {
	rcvbuf => 2**12,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
