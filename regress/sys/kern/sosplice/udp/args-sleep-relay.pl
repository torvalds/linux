# test with multiple packets, relay sleeps before processing

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (0, 1, 2, 3, 4, 5);

our %args = (
    client => {
	lengths => \@lengths,
    },
    relay => {
	func => sub { sleep 3; relay(@_); },
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "553af7b8fc0e205ead2562ab61a2ad13",
);
