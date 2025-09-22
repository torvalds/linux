# test inline out-of-band data with maximum data length delay before relay

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_oob(@_); },
	nocheck => 1,
    },
    relay => {
	func => sub { sleep 3; relay(@_); },
	oobinline => 1,
	max => 61,
	nocheck => 1,
    },
    server => {
	func => \&read_oob,
    },
    len => 61,
    # the oob data is converted to non-oob
    md5 => "4b5efc5f86fa5fc873c82103ceece85d",
);
