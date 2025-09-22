use strict;
use warnings;

my %header = ( "Host" => "www.example.com", "Set-Cookie" => "a="."X"x8192 );
our %args = (
    client => {
	func => \&http_client,
	header => \%header,
	httpnok => 1,
	nocheck => 1,
	loggrep => qr/HTTP\/1\.0 413 Payload Too Large/,
    },
    relayd => {
	protocol => [ "http",
	    'return error',
	    'pass',
	],
	loggrep => qr/413 Payload Too Large/,
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
