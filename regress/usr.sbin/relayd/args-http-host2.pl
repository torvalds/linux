use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<'EOF';
GET http://www.foo.com/1 HTTP/1.1

EOF
	    http_response($self, 1);
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
	    qr/, malformed header$/ => 0,
	    qr/\[http:\/\/www.foo.com\/1\] GET/ => 1,
	},
    },
    server => {
	func => \&http_server,
	nocheck => 1,
    }
);

1;
