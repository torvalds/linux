# test client ssl certificate verification

use strict;
use warnings;

our %args = (
    client => {
	ssl => 1,
	offertlscert => 1,
    },
    relayd => {
	listenssl => 1,
	verifyclient => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);

1;
