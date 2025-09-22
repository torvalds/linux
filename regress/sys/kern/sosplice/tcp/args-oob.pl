# test out-of-band data

use strict;
use warnings;

our %args = (
    client => {
	func => \&write_oob,
	nocheck => 1,
    },
    relay => {
	nocheck => 1,
    },
    server => {
	func => \&read_oob,
    },
    len => 247,
    md5 => "f76df02a35322058b8c29002aaea944f",
);
