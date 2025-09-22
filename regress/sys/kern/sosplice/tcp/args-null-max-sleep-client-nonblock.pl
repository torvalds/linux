# test maximum data length with empty client write,
# client sleeps before writing,
# relay is non-blocking

use strict;
use warnings;

our %args = (
    client => {
	func => sub { sleep 3; write_stream(@_); },
	len => 0,
    },
    relay => {
	max => 113,
	nomax => 1,
	nonblocking => 1,
    },
    len => 0,
    md5 => "d41d8cd98f00b204e9800998ecf8427e",
);
