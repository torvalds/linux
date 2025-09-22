# test reverse sending with non-blocking relay

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_stream,
    },
    relay => {
	func => sub { ioflip(@_); relay(@_); },
	nonblocking => 1,
    },
    server => {
	func => \&write_stream,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);
