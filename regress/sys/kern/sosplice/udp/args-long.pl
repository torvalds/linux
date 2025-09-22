# test longer data length

use strict;
use warnings;

our %args = (
    client => {
	len => 2**16 - 20 - 8 - 1,
	sndbuf => 2**16,
    },
    relay => {
	idle => 6,
	size => 2**16,
	sndbuf => 2**16,
	rcvbuf => 2**16,
    },
    server => {
	idle => 7,
	rcvbuf => 2**16,
    },
    len => 65507,
    md5 => "20f83f523b6b48b11d9f8a15e507e16a",
);
