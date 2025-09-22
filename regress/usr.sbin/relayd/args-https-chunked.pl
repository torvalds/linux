# test chunked https 1.1 connection over http relay

use strict;
use warnings;

my @lengths = ([ 251, 10000, 10 ], 1, [2, 3]);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	http_vers => ["1.1"],
	ssl => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log Transfer-Encoding",
	],
	loggrep => {
		"{Transfer-Encoding: chunked}" => 1,
		qr/\[\(null\)\]/ => 0,
	},
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	func => \&http_server,
	ssl => 1,
    },
    lengths => \@lengths,
    md5 => [
	"bc3a3f39af35fe5b1687903da2b00c7f",
	"fccd8d69acceb0cc35f2fd4e2f6938d3",
	"c47658d102d5b989e0da09ce403f7463",
	"68b329da9893e34099c7d8ad5cb9c940",
	"897316929176464ebc9ad085f31e7284",
	"0ade138937c4b9cb36a28e2edb6485fc",
    ],
);

1;
