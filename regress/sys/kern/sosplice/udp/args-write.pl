# test mix write and relay

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (1, 2, 3, 4, 5);

our %args = (
    client => {
	lengths => [ 2, 3, 4 ],
	nocheck => 1,
    },
    relay => {
	func => sub {
	    write_stream(@_, 1);
	    IO::Handle::flush(\*STDOUT);
	    relay(@_);
	    write_stream(@_, 5);
	},
	nocheck => 1,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "1830da3b4358ccedeb49877c2cff8c86",
);
