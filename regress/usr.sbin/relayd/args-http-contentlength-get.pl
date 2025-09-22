# Test to verify that relayd strips Content-Length and body
# from GET requests.

use strict;
use warnings;

my $payload_len = 64;
our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    my @request_stream = split("\n", <<"EOF", -1);
GET http://foo.bar/$payload_len HTTP/1.1
Content-Length: $payload_len

foo=bar

EOF
	    pop @request_stream;
	    print map { "$_\r\n" } @request_stream;
	    print STDERR map { ">>> $_\n" } @request_stream;
	    $self->{method} = 'GET';
	    http_response($self, $payload_len);
	},
	loggrep => {
	    qr/Content-Length: $payload_len/ => 2,
	    qr/foo=bar/ => 1,
	},
	http_vers => ["1.1"],
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request path log \"*\"",
	],
	loggrep => {
	    qr/, done, \[http:\/\/foo.bar\/$payload_len\] GET/ => 1,
	},
    },
    server => {
	func => \&http_server,
	loggrep => {
	    qr/Content-Length: $payload_len/ => 1,
	    qr/foo=bar/ => 0,
	},
	nocheck => 1,
    },
);

1;
