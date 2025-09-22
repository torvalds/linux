# Test parsing of invalid chunk length values
# We force multiple connections since relayd will abort the connection
# when it encounters a bogus chunk size.
# 

use strict;
use warnings;

my @lengths = (7, 6, 5, 4, 3, 2);
my @chunks = ("0x4", "+3", "-0", "foo", "dead beef", "Ff0");
our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    my $chunk = shift(@chunks);
	    $self->{redo} = int(@chunks);
	    print <<"EOF";
PUT /4/3 HTTP/1.1
Host: foo.bar
Transfer-Encoding: chunked

$chunk

EOF
	    foreach (@lengths) {
		print STDERR "LEN: $_\n";
	    }
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
	    qr/, invalid chunk size, PUT/ => 5,
	},
    },
    server => {
	func => \&http_server,
	nocheck => 1,
    },
    lengths => \@lengths,
);

1;
