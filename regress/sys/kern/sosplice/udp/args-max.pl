# test maximum data length

use strict;
use warnings;

our %args = (
    client => {
	lengths => [ 1, 3, 1 ],
	func => sub { errignore(@_); write_datagram(@_); },
	nocheck => 1,
    },
    relay => {
	max => 4,
	big => 1,
    },
    len => 4,
    lengths => "1 3",
    md5 => "5df2ad5a8fa778004d06dc070811754c",
);
