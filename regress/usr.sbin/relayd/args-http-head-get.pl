use strict;
use warnings;

my $payload_len = 64;
our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    my @request_stream = split("\n", <<"EOF", -1);
HEAD http://foo.bar/$payload_len HTTP/1.1

EOF
	    pop @request_stream;
	    print map { "$_\r\n" } @request_stream;
	    print STDERR map { ">>> $_\n" } @request_stream;
	    $self->{method} = 'HEAD';
	    http_response($self, $payload_len);
	    @request_stream = split("\n", <<"EOF", -1);
GET http://foo.bar/$payload_len HTTP/1.1

EOF
	    pop @request_stream;
	    print map { "$_\r\n" } @request_stream;
	    print STDERR map { ">>> $_\n" } @request_stream;
	    $self->{method} = 'GET';
	    http_response($self, $payload_len);
	},
	http_vers => ["1.1"],
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request path log \"*\"",
	],
	loggrep => {
	    qr/, done, \[http:\/\/foo.bar\/$payload_len\] HEAD; \[http:\/\/foo.bar\/$payload_len\] GET/ => 1,
	},
    },
    server => {
	func => \&http_server,
	nocheck => 1,
    },
);

1;
