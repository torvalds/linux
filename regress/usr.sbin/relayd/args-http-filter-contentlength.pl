# test http connection with request filter and explicit content length 0

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	header => {
	    'Content-Length' => 0,
	},
	len => 1,
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
