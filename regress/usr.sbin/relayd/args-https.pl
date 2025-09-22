# test https connection over http relay

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	ssl => 1,
	loggrep => 'Issuer.*/OU=relayd/',
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log bar",
	],
	forwardssl => 1,
	listenssl => 1,

    },
    server => {
	func => \&http_server,
	ssl => 1,
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);

1;
