# test that 3 seconds timeout does not occur within 2 seconds idle in http

use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	len => 5,
	timefile => "",
    },
    relayd => {
	protocol => [ "http" ],
	relay => [ "session timeout 3" ],
	loggrep => { qr/(buffer event|splice) timeout/ => 0 },
    },
    server => {
	func => sub {
	    errignore();
	    http_server(@_);
	    sleep 2;
	    write_char(@_, 4);
	},
	sleep => 1,
	nocheck => 1,
    },
    len => 9,
);

1;
