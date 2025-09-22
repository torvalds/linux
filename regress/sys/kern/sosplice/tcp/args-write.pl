# test mix write and relay

use strict;
use warnings;

our %args = (
    client => {
	len => 65521,
	nocheck => 1,
    },
    relay => {
	func => sub {
	    write_stream(@_, 32749);
	    IO::Handle::flush(\*STDOUT);
	    relay(@_);
	    write_stream(@_, 2039);
	},
	nocheck => 1,
    },
    len => 100309,
    md5 => "0efc7833e5c0652823ca63eaccb9918f",
);
