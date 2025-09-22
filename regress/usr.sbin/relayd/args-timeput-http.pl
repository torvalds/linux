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
    },
    relayd => {
	relay => [ "session timeout 2" ],
	loggrep => { qr/(buffer event|splice) timeout/ => 0 },
    },
    server => {
	func => \&http_server,
	method => "PUT",
    },
    len => 5,
);

1;
