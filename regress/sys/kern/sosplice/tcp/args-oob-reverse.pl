# test out-of-band data when reverse sending

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_oob,
    },
    relay => {
	func => sub { ioflip(@_); relay(@_); },
	nocheck => 1,
    },
    server => {
	func => \&write_oob,
	nocheck => 1,
    },
    len => 247,
    md5 => "f76df02a35322058b8c29002aaea944f",
);
