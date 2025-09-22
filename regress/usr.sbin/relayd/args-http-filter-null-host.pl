# test http request with null Host header

use strict;
use warnings;


my %header_client = (
    "Host" => "",
);

our %args = (
    client => {
	func => \&http_client,
	header => \%header_client,
	httpnok => 1,
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    'pass',
	    'block url "Host"',
	    'return error',
	],
#	loggrep => qr/Forbidden, \[Cookie: med=thx.*/,
    },
    server => {
	func => \&http_server,
	noserver => 1,
	nocheck => 1,
    },
);

1;
