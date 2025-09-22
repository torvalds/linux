# test maximum data length with short data stream,
# relay sleeps before processing

use strict;
use warnings;

our %args = (
    client => {
	nocheck => 1,
    },
    relay => {
	func => sub { sleep 3; relay(@_); },
	max => 113,
	big => 1,
    },
    len => 113,
    md5 => "dc099ef642faa02bce71298f11e7d44d",
);
