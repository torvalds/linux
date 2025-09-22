# test 50 http get with length 1 over http relay

use strict;
use warnings;

my @lengths = map { 1 } (1..50);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	method => "GET",
    },
    relayd => {
	protocol => [ "http" ],
	loggrep => {
	    qr/, (?:done|last write \(done\)), GET/ => (1 + @lengths),
	},
    },
    server => {
	func => \&http_server,
    },
    lengths => \@lengths,
);

1;
