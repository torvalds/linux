# test that 3 seconds timeout occurs within 4 seconds idle in http

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	len => 5,
	timefile => "",
	ssl => 1,
	loggrep => 'Issuer.*/OU=relayd/',
    },
    relayd => {
	protocol => [ "http" ],
	relay => [ "session timeout 3" ],
	loggrep => { qr/buffer event timeout/ => 1 },
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	func => sub {
	    errignore();
	    http_server(@_);
	    sleep 4;
	    write_char(@_, 4);
	},
	sleep => 1,
	down => "Broken pipe",
	nocheck => 1,
	ssl => 1,
    },
    len => 5,
);

1;
