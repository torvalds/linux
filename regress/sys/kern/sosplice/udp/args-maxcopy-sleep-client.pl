# test maximum data length then copy packets,
# client sleeps before and during writing

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (5, 4, 3, 2, 1, 0);

our %args = (
    client => {
	func => sub { sleep 3; write_datagram(@_); },
	sleep => 1,
	lengths => \@lengths,
    },
    relay => {
	idle => 5,
	func => sub { relay(@_); relay_copy(@_); },
	max => 9,
	big => 1,
	timeout => 1,
	nocheck => 1,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "464ddb107046ee0a42f43b202e826b8f",
);
