# test that 3 seconds timeout occurs within 4 seconds idle

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    errignore();
	    write_char(@_, 5);
	    sleep 4;
	    write_char(@_, 4);
	},
	sleep => 1,
	down => "Broken pipe",
	timefile => "",
	nocheck => 1,
	ssl => 1,
	loggrep => 'Issuer.*/OU=relayd/',
    },
    relayd => {
	relay => [ "session timeout 3" ],
	loggrep => { qr/buffer event timeout/ => 1 },
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	ssl => 1,
    },
    len => 5,
);

1;
