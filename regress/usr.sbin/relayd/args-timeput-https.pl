# test that 2 seconds timeout does not occur while client writes for 4 seconds

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	len => 5,
	sleep => 1,
	method => "PUT",
	timefile => "",
	ssl => 1,
	loggrep => 'Issuer.*/OU=relayd/',
    },
    relayd => {
	relay => [ "session timeout 2" ],
	loggrep => { qr/buffer event timeout/ => 0 },
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	func => \&http_server,
	method => "PUT",
	ssl => 1,
    },
    len => 5,
);

1;
