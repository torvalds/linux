use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<'EOF';
GET http://www.foo.com/1 HTTP/1.1
Host: www.bar.com

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
	    "match request path log \"*\"",
	],
	loggrep => {
	    qr/, malformed host$/ => 1,
	    qr/\[http:\/\/www.foo.com\/1\] GET/ => 0,
	    qr/\[Host: www.bar.com\]/ => 0,
	},
    },
    server => {
	func => \&http_server,
	noserver => 1,
	nocheck => 1,
    }
);

1;
