use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	loggrep => {
	    qr/403 Forbidden/ => 1,
	    qr/Content-Type: text\/html/ => 1
	},
	path => "query?foo=bar&ok=yes",
	httpnok => 1,
	nocheck => 1,
    },
    relayd => {
	protocol => [ "http",
	    'return error',
	    'block',
	    'match request query log "foo" value "bar" label "expect_foobar_label"',
	],
	loggrep => qr/Forbidden.*403 Forbidden.*expect_foobar_label.*foo: bar/,
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
