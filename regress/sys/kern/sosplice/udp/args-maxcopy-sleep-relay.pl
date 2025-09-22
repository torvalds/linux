# test maximum data length then copy packets,
# relay sleeps before processing

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (5, 4, 3, 2, 1, 0);

our %args = (
    client => {
	lengths => \@lengths,
    },
    relay => {
	func => sub { sleep 3; relay(@_); relay_copy(@_); },
	max => 9,
	big => 1,
	timeout => 1,
	nocheck => 1,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "464ddb107046ee0a42f43b202e826b8f",
);
