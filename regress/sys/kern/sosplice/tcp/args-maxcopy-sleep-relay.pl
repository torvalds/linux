# test maximum data length then copy stream,
# relay sleeps before processing

use strict;
use warnings;

our %args = (
    relay => {
	func => sub { sleep 3; relay(@_); relay_copy(@_); },
	max => 197,
	big => 1,
	end => 1,
	nocheck => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);
