# test http put with request filter

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	len => 1,
	method => 'PUT',
    },
    relayd => {
	protocol => [ "http",
	    'block request path "/2"',
	],
	loggrep => qr/done/,
    },
    server => {
	func => \&http_server,
    },
    len => 1,
    md5 => "68b329da9893e34099c7d8ad5cb9c940",
);

1;
