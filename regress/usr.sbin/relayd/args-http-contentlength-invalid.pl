# Test that relayd aborts the connection if Content-Length is invalid
# We test "+0" because it is accepted by strtol(), sscanf(), etc
# but is not legal according to the RFC.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<"EOF";
PUT /1 HTTP/1.1
Host: www.foo.com
Content-Length: +0

EOF
	    # no http_response($self, 1);
	},
	http_vers => ["1.1"],
	nocheck => 1,
	method => "PUT",
    },
    relayd => {
	protocol => [ "http",
	    "match request header log Host",
	],
	loggrep => {
	    qr/, invalid$/ => 1,
	    qr/\[Host: www.foo.com\] PUT/ => 0,
	},
    },
    server => {
	func => \&http_server,
	nocheck => 1,
	noserver => 1,
    }
);

1;
