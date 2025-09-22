use strict;
use warnings;

my @lengths = (21);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	path => "query?foo=bar&ok=yes",
    },
    relayd => {
	protocol => [ "http",
	    'block request',
	    'block request query log "ok"',
	    'pass query log "foo" value "bar"',
	],
	loggrep => {
		qr/\[foo: bar\]/ => 2,
		qr/\[ok: yes\]/ => 0,
	},
    },
    server => {
	func => \&http_server,
    },
    lengths => \@lengths,
);

1;
