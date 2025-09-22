# test sending an udp packet that does not fit into splice space

use strict;
use warnings;

our %args = (
    client => {
	lengths => [ 1, 10000, 2, 10001, 3 ],
	sndbuf => 30000,
	nocheck => 1,
    },
    relay => {
	func => sub { sleep 3; errignore(@_); relay(@_); },
	down => "Message too long",
	rcvbuf => 30000,
	sndbuf => 10000,
	nocheck => 1,
    },
    server => {
	rcvbuf => 30000,
    },
    len => 10003,
    lengths => "1 10000 2",
    md5 => "2ec9a4b45a449095245177d2cf51dd24",
);
