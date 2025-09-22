# test idle timeout

use strict;
use warnings;

our %args = (
    client => {
	func => sub { sleep 1; write_stream(@_); sleep 3; },
    },
    relay => {
	idle => 2,
	timeout => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);
