# test out-of-band data with maximum data length delay before server

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_oob(@_); },
	nocheck => 1,
    },
    relay => {
	max => 61,
    },
    server => {
	func => sub { sleep 3; read_oob(@_); },
    },
    len => 61,
    md5 => "e4282daf8d2ca21cc8b70b1314713314",
    noecho => 1,
);
