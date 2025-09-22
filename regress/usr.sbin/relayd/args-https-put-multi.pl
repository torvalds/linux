# test 50 https put with length 1 over http relay

use strict;
use warnings;

my @lengths = map { 1 } (1..50);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	method => "PUT",
	ssl => 1,
    },
    relayd => {
	protocol => [ "http" ],
	loggrep => {
	    qr/, (?:done|last write \(done\)), PUT/ => (1 + @lengths),
	},
	forwardssl => 1,
	listenssl => 1,
    },
    server => {
	func => \&http_server,
	ssl => 1,
    },
    lengths => \@lengths,
);

1;
