# test with delay before empty client write

use strict;
use warnings;

our %args = (
    client => {
	func => sub { sleep 3; write_stream(@_); },
	len => 0,
    },
    len => 0,
    md5 => "d41d8cd98f00b204e9800998ecf8427e",
);
