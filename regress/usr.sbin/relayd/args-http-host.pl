use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<'EOF';
GET /1 HTTP/1.1
Host: www.foo.com
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
	],
	loggrep => {
	    qr/, malformed header$/ => 1,
	    qr/\[Host: www.foo.com\] GET/ => 0,
	    qr/\[Host: www.bar.com\] GET/ => 0,
	},
    },
    server => {
	func => \&http_server,
	noserver => 1,
	nocheck => 1,
    }
);

1;
