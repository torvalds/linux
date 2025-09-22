# test maximum data length with maximum reached and multiple packets in rcvbuf

use strict;
use warnings;

our %args = (
    client => {
	lengths => [ 1, 2, 3 ],
	nocheck => 1,
    },
    relay => {
	funcs => sub { sleep 3; relay(@_); },
	max => 4,
	big => 0,
    },
    len => 3,
    lengths => "1 2",
    md5 => "52f58714e430f1fc84346961c240054b",
);
