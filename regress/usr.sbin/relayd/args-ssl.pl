# test both client and server ssl connection

use strict;
use warnings;

our %args = (
    client => {
	ssl => 1,
	loggrep => 'Issuer.*/OU=relayd/',
    },
    relayd => {
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	ssl => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);

1;
