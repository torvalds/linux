# test both client and server ssl connection with TLS inspection

use strict;
use warnings;

our %args = (
    client => {
	ssl => 1,
	loggrep => 'Issuer.*/OU=ca/',
    },
    relayd => {
	inspectssl => 1,
    },
    server => {
	ssl => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);

1;
