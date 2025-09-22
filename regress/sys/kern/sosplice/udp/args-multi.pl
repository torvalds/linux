# test with mutiple packets

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (251, 16384, 0, 1, 2, 3, 4, 5);

our %args = (
    client => {
	lengths => \@lengths,
	sndbuf => 30000,
    },
    relay => {
	rcvbuf => 30000,
	sndbuf => 30000,
    },
    server => {
	rcvbuf => 30000,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "544464f20384567028998e1a1a4c5b1e",
);
