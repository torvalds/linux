# test router priority with one interface state machine (ism)
# ospfd has prio 0, ism of the test has prio 1
# test that ism gets dr, there is no bdr

use strict;
use warnings;
use Default qw($area $tap_number $ospfd_ip $ospfd_rtrid);

our %tst_args = (
    ospfd => {
	conf => {
	    areas => {
		$area => {
		    "tap$tap_number:$ospfd_ip" => {
			'router-priority' => '0',
		    },
		},
	    },
	},
    },
    client => {
	state => {
	    pri => 1,
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
		    # XXX dr flipping between "0.0.0.0" and "10.188.6.18"
		    bdr => "0.0.0.0",
		},
		wait => {
		    nbrs => [ "10.188.0.18" ],
		},
		timeout => 5,  # 2 * hello interval + 1 second
	    },
	    {
		name => "we are 2-way, wait for dr 10.188.6.18 and ".
		    "no bdr in received hello",
		check => {
		    nbrs => [ "10.188.0.18" ],
		},
		wait => {
		    dr => "10.188.6.18",
		    bdr  => "0.0.0.0",
		},
		timeout => 11,  # dead interval + hello interval + 1 second
	    },
	],
    },
);

1;
