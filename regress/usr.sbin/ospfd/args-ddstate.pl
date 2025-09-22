use strict;
use warnings;
use Default qw($ospfd_ip $ospfd_rtrid);

our %tst_args = (
    client => {
	tasks => [
	    {
		name => "receive hello with dr 0.0.0.0 bdr 0.0.0.0, ".
		    "enter $ospfd_rtrid as our neighbor",
		check => {
		    dr  => "0.0.0.0",
		    bdr => "0.0.0.0",
		    nbrs => [],
		},
		state => {
		    nbrs => [ $ospfd_rtrid ],
		},
	    },
	    {
		name => "neighbor asserting itself as master. " .
		    "We proclaim to be master, because of higher router id.",
		wait => {
		    dd_bits => 7, # I|M|MS
		},
		state => {
		    dd_bits => 7,
		},
		timeout => 10, # not specified in rfc
	    },
	    {
		name => "check if neighbor is slave, initialization is done ".
		    "and neighbour has applied our dd sequence number.",
		wait => {
		    dd_bits => 0x2, # M
		    dd_seq => 999,
		},
		timeout => 10, # not specified in rfc
	    },
	],
    },
);

1;
