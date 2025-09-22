# test connection reset by server

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**17,
	sndbuf => 2**15,
    },
    relay => {
	func => sub { errignore(@_); relay(@_); },
	rcvbuf => 2**12,
	sndbuf => 2**12,
	down => "Broken pipe|Connection reset by peer",
    },
    server => {
	func => sub { sleep 3; solingerin(@_); },
	rcvbuf => 2**12,
    },
    nocheck => 1,
    noecho => 1,
);
