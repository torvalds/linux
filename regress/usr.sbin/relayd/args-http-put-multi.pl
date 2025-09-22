# test 50 http put with length 1 over http relay

use strict;
use warnings;

my @lengths = map { 1 } (1..50);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	method => "PUT",
    },
    relayd => {
	protocol => [ "http" ],
	loggrep => {
	    qr/, (?:done|last write \(done\)), PUT/ => (1 + @lengths),
	},
    },
    server => {
	func => \&http_server,
    },
    lengths => \@lengths,
);

1;
