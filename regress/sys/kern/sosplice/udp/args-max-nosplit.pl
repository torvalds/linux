# test maximum data length, packet cannot be split at maximum

use strict;
use warnings;

our %args = (
    client => {
	lengths => [ 1, 3, 1 ],
	func => sub { errignore(@_); write_datagram(@_); },
	nocheck => 1,
    },
    relay => {
	max => 2,
	big => 0,
    },
    len => 1,
    lengths => "1",
    md5 => "68b329da9893e34099c7d8ad5cb9c940",
);
