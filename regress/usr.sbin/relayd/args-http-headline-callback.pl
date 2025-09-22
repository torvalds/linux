# test persistent http connection over http relay invoking the callback
# The client writes a bad header line in the second request.
# Check that the relay handles the input after the error correctly.

use strict;
use warnings;

my @lengths = (4, 3);
our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<'EOF';
PUT /4 HTTP/1.1
Host: foo.bar
Content-Length: 4

123
PUT /3 HTTP/1.1
XXX
Host: foo.bar
Content-Length: 3

12
EOF
	    print STDERR "LEN: 4\n";
	    print STDERR "LEN: 3\n";
	    # relayd does not forward the first request if the second one
	    # is invalid.  So do not expect any response.
	    #http_response($self, "without len");
	},
	http_vers => ["1.1"],
	lengths => \@lengths,
	method => "PUT",
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log bar",
	],
	loggrep => {
	    qr/, malformed, PUT/ => 1,
	},
    },
    server => {
	func => \&http_server,
	# The server does not get any connection.
	noserver => 1,
	nocheck => 1,
    },
    lengths => \@lengths,
);

1;
