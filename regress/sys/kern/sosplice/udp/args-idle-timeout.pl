# test idle timeout, must not be too long

use strict;
use warnings;

my @lengths = (1, 2);

our %args = (
    client => {
	func => sub { sleep 1; write_datagram(@_); },
	lengths => \@lengths,
	sleep => 3,
	nocheck => 1,
    },
    relay => {
	idle => 2,
	timeout => 1,
    },
    len => 1,
    lengths => "1",
    md5 => "68b329da9893e34099c7d8ad5cb9c940",
);
