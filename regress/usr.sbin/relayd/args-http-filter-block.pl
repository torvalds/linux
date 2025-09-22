# test http block

use strict;
use warnings;

my @lengths = (1, 2, 0, 3);
our %args = (
    client => {
	func => \&http_client,
	loggrep => qr/Client missing http 3 response/,
	lengths => \@lengths,
    },
    relayd => {
	protocol => [ "http",
	    'block request path "/3"',
	],
	loggrep => qr/Forbidden/,
    },
    server => {
	func => \&http_server,
    },
    lengths => [1, 2, 0],
);

1;
