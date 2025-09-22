# test relay closes stdin after relaying some data

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**30,  # not reached
	sndbuf => 2**10,  # small buffer triggers error during write
	# the error message seems to be timing dependent
	down => "Client print failed: (Broken pipe|Connection reset by peer)",
	nocheck => 1,
	error => 54,
    },
    relay => {
	func => sub { errignore(@_); $SIG{ALRM} = sub { close STDIN };
	  alarm(3); relay(@_); },
	rcvbuf => 2**10,
	sndbuf => 2**10,
	down => "Bad file descriptor",
	nocheck => 1,
	errorin => "",
    },
    server => {
	rcvbuf => 2**10,
	nocheck => 1,
    },
);
