use strict;
use warnings;

my @lengths = (1, 2, 4, 0, 3, 5);
our %args = (
    client => {
	func => \&http_client,
	lengths => \@lengths,
	loggrep => {
		qr/403 Forbidden/ => 4,
	},
    },
    relayd => {
	protocol => [ "http",
	    'return error',
	    'pass',
	    'block request url log file "$curdir/args-http-filter-url-file.in" value "*" label "test_reject_label"',
	],
	loggrep => {
		qr/Forbidden/ => 4,
		qr/\[test_reject_label\, foo\.bar\/0\]/ => 2,
		qr/\[test_reject_label\, foo\.bar\/3\]/ => 2,
	},
    },
    server => {
	func => \&http_server,
    },
    lengths => [1, 2, 4, 5],
);

1;
