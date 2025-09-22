# test that 2 seconds timeout does not occur while server writes for 4 seconds

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_char,
	timefile => "",
    },
    relayd => {
	relay => [ "session timeout 2" ],
	loggrep => { qr/(buffer event|splice) timeout/ => 0 },
    },
    server => {
	func => \&write_char,
	len => 5,
	sleep => 1,
    },
    len => 5,
);

1;
