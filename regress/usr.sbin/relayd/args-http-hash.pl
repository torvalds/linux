use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	path => "query?foobar",
    },
    relayd => {
	table => 1,
	protocol => [ "http",
	    'match request path hash "/query"',
	    'match request path log "/query"',
	],
	relay => 'forward to <table-$test> port $connectport',
	loggrep => {
		qr/ (?:done|last write \(done\)), \[\/query: foobar\]/ => 1,
	},
    },
    server => {
	func => \&http_server,
    },
    len => 13,
);

1;
