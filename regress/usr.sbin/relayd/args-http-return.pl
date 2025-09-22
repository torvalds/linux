use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	# XXX add more paths to match a case where it pass
	path => "query?foo=bar&ok=yes",
	nocheck => 1,
	httpnok => 1,
    },
    relayd => {
	protocol => [ "http",
	    'return error',
	    'pass',
	    'block request query log "foo" value "bar" label \
		"expect_foobar_return_test"',
	],
	loggrep => { 'Forbidden \(403 Forbidden\), \[expect_foobar_return_test, foo: bar\]' => 1 },
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
