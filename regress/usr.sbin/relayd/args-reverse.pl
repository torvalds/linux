# test reverse sending from server to client

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_char,
    },
    server => {
	func => \&write_char,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);

1;
