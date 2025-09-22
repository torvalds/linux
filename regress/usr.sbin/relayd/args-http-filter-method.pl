# test method filtering

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	method => 'HEAD',
	loggrep => qr/HTTP\/1\.0 403 Forbidden/,
	httpnok => 1,
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    'return error',
	    'block request method "HEAD" path log',
	],
	loggrep => qr/403 Forbidden/,
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
