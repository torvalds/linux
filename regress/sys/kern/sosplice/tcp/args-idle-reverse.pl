# test non idle connection while reverse splicing gets timeout

use strict;
use warnings;
use BSD::Socket::Splice qw(setsplice);

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 6,
	sleep => 1,
    },
    relay => {
	func => sub {
	    setsplice(\*STDOUT, \*STDIN, undef, 3)
		or die "reverse splice failed: $!";
	    relay(@_);
	},
	idle => 2,
	nocheck => 1,
    },
    len => 6,
    md5 => "857f2261690a2305dba03062e778a73b",
    noecho => 1,
);
