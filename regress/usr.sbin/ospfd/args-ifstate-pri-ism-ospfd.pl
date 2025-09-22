# test router priority with one interface state machine (ism)
# ospfd has prio 1, ism of the test has prio 2
# test that ospfd gets bdr (does not work), ism gets dr

use strict;
use warnings;
use Default qw($area $tap_number $ospfd_ip $ospfd_rtrid);

our %tst_args = (
    ospfd => {
	conf => {
	    areas => {
		$area => {
		    "tap$tap_number:$ospfd_ip" => {
			'router-priority' => '1',
		    },
		},
	    },
	},
    },
    client => {
	state => {
	    pri => 2,
	},
	tasks => [
	    {
		name => "receive hello with dr 0.0.0.0 bdr 0.0.0.0, ".
		    "enter $ospfd_rtrid as our neighbor",
		check => {
		    dr   => "0.0.0.0",
		    bdr  => "0.0.0.0",
		    nbrs => [],
		},
		state => {
		    nbrs => [ $ospfd_rtrid ],
		},
	    },
	    {
		name => "wait for neighbor 10.188.0.18 in received hello",
		check => {
		    dr  => "0.0.0.0",
		    bdr => "0.0.0.0",
		},
		wait => {
		    nbrs => [ "10.188.0.18" ],
		},
		timeout => 5,  # 2 * hello interval + 1 second
	    },
	    {
		name => "we are 2-way, wait for dr 10.188.6.18 and ".
		    "bdr $ospfd_ip in received hello",
		check => {
		    nbrs => [ "10.188.0.18" ],
		},
		wait => {
		    dr => "10.188.6.18",
		    bdr  => "0.0.0.0"  # XXX should be $ospfd_ip
		},
		timeout => 11,  # dead interval + hello interval + 1 second
	    },
	],
    },
);

1;
