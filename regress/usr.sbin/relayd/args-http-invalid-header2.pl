# Test that relayd aborts the connection if a header include a NUL byte

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<"EOF";
GET /1 HTTP/1.1
Host: www.foo.com
X-Header-Client: ABC\0D

EOF
	    # no http_response($self, 1);
	},
	http_vers => ["1.1"],
	nocheck => 1,
	method => "GET",
    },
    relayd => {
	protocol => [ "http",
	    "match request header log Host",
	],
	loggrep => {
	    qr/, malformed$/ => 1,
	    qr/\[Host: www.foo.com\] GET/ => 0,
	},
    },
    server => {
	func => \&http_server,
	nocheck => 1,
	noserver => 1,
    }
);

1;
