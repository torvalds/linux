# test inline out-of-band data with maximum data length

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_oob(@_); },
	nocheck => 1,
    },
    relay => {
	oobinline => 1,
	max => 61,
	nocheck => 1,
    },
    server => {
	func => \&read_oob,
    },
    len => 61,
    md5 => [
	"c9f459db9b4f369980c79bff17e1c2a0",
	"4b5efc5f86fa5fc873c82103ceece85d",
    ],
    noecho => 1,
);
