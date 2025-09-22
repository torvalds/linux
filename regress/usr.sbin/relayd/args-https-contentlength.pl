# test persistent https 1.1 connection and grep for content length

use strict;
use warnings;

my @lengths = (1, 2, 0, 3, 4);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	ssl => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log Content-Length",
	],
	loggrep => [ map { "Content-Length: $_" } @lengths ],
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	func => \&http_server,
	ssl => 1,
    },
    lengths => \@lengths,
    md5 => [
	"68b329da9893e34099c7d8ad5cb9c940",
	"897316929176464ebc9ad085f31e7284",
	"d41d8cd98f00b204e9800998ecf8427e",
	"0ade138937c4b9cb36a28e2edb6485fc",
	"e686f5db1f8610b65f98f3718e1a5b72",
	"68b329da9893e34099c7d8ad5cb9c940",
	"897316929176464ebc9ad085f31e7284",
	"d41d8cd98f00b204e9800998ecf8427e",
	"0ade138937c4b9cb36a28e2edb6485fc",
	"e686f5db1f8610b65f98f3718e1a5b72",
    ],
);

1;
