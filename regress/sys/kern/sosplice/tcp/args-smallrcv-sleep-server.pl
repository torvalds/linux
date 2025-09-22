# test with smaller relay receive buffer delay before server

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    relay => {
	rcvbuf => 2**12,
    },
    server => {
	func => sub { sleep 3; read_stream(@_); },
	rcvbuf => 2**15,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
