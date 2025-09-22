# test with multiple packets, client sleeps before and during send

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (0, 1, 2, 3, 4, 5);

our %args = (
    client => {
	func => sub { sleep 3; write_datagram(@_); },
	lengths => \@lengths,
	sleep => 1,
    },
    relay => {
	idle => 5,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "553af7b8fc0e205ead2562ab61a2ad13",
);
