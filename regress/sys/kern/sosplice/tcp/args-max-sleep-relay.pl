# test maximum data length then close stdin,
# relay sleeps before processing

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**17,
	sndbuf => 2**15,
	down => "Client print failed: Broken pipe",
	nocheck => 1,
    },
    relay => {
	func => sub { sleep 3; relay(@_); shutin(@_); sleep 1; },
	max => 32117,
	rcvbuf => 2**15,
	big => 1,
    },
    len => 32117,
    md5 => "ee338e9693fb2a2ec101bb28935ed123",
);
