# test broken config

use strict;
use warnings;

our %args = (
    client => {
	noclient => 1,
    },
    relayd => {
	protocol => [ "foo" ],
	dryrun => qr/invalid protocol type: foo/,
    },
    server => {
	noserver => 1,
    },
    nocheck => 1,
);

1;
