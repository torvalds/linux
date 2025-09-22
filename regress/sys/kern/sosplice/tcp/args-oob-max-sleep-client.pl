# test out-of-band data with maximum data length delay before client

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); sleep 3; write_oob(@_); },
	nocheck => 1,
    },
    relay => {
	max => 61,
	nocheck => 1,
    },
    server => {
	func => \&read_oob,
    },
    len => 61,
    md5 => "e4282daf8d2ca21cc8b70b1314713314",
);
