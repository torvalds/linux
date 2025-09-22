# test persistent https 1.1 connection over http relay

use strict;
use warnings;

my @lengths = (251, 16384, 0, 1, 2, 3, 4, 5);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	ssl => 1,
    },
    relayd => {
	protocol => [ "http",
	    "match request header log foo",
	    "match response header log bar",
	],
	loggrep => {
	    qr/, (?:done|last write \(done\))/ => (1 + @lengths),
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
	"52afece07e61264c3087ddf52f729376",
	"d41d8cd98f00b204e9800998ecf8427e",
	"68b329da9893e34099c7d8ad5cb9c940",
	"897316929176464ebc9ad085f31e7284",
	"0ade138937c4b9cb36a28e2edb6485fc",
	"e686f5db1f8610b65f98f3718e1a5b72",
	"e5870c1091c20ed693976546d23b4841",
	"bc3a3f39af35fe5b1687903da2b00c7f",
	"52afece07e61264c3087ddf52f729376",
	"d41d8cd98f00b204e9800998ecf8427e",
	"68b329da9893e34099c7d8ad5cb9c940",
	"897316929176464ebc9ad085f31e7284",
	"0ade138937c4b9cb36a28e2edb6485fc",
	"e686f5db1f8610b65f98f3718e1a5b72",
	"e5870c1091c20ed693976546d23b4841",
    ],
);

1;
