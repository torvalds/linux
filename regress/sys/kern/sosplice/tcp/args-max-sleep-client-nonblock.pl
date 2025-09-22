# test maximum data length,
# client sleeps before writing,
# relay is non-blocking

use strict;
use warnings;

our %args = (
    client => {
	func => sub { sleep 3; write_stream(@_); },
	nocheck => 1,
    },
    relay => {
	max => 113,
	big => 1,
	nonblocking => 1,
    },
    len => 113,
    md5 => "dc099ef642faa02bce71298f11e7d44d",
);
