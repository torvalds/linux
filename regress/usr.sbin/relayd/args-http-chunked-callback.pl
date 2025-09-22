# test chunked http connection over http relay invoking the callback
# The client writes a bad chunk length in the second chunk.
# Check that the relay handles the input after the error correctly.

use strict;
use warnings;

my @lengths = ([4, 3]);
our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    print <<'EOF';
PUT /4/3 HTTP/1.1
Host: foo.bar
Transfer-Encoding: chunked

EOF
	    ${$self->{server}}->up;
	    print <<'EOF';
4
123

XXX
3
12

0

EOF
	    print STDERR "LEN: 4\n";
	    print STDERR "LEN: 3\n";
	    # relayd does not forward the first chunk if the second one
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
	    qr/, invalid chunk size, PUT/ => 1,
	},
    },
    server => {
	down => "Server missing chunk size",
	func => sub { errignore(@_); http_server(@_); },
	nocheck => 1,
    },
    lengths => \@lengths,
);

1;
