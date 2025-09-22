# test ospfd without any interface state machines
# the ospfd will get dr and there must be no neighbors

use strict;
use warnings;
use Default qw($ospfd_ip);

our %tst_args = (
    client => {
	state => [],
	tasks => [
	    {
		name => "receive hello with dr 0.0.0.0 bdr 0.0.0.0",
		check => {
		    dr  => "0.0.0.0",
		    bdr => "0.0.0.0",
		    nbrs => [],
		},
	    },
	    {
		name => "there must be no nbrs, wait until dr $ospfd_ip",
		check => {
		    bdr => "0.0.0.0",
		    nbrs => [],
		},
		wait => {
		    dr => $ospfd_ip,
		},
		timeout => 11,  # dead interval + hello interval + 1 second
	    },
	],
    },
);

1;
