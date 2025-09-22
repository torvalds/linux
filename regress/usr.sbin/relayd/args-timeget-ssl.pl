# test that 2 seconds timeout does not occur while server writes for 4 seconds

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_char,
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
	func => \&write_char,
	len => 5,
	sleep => 1,
	ssl => 1,
    },
    len => 5,
);

1;
