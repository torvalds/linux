use strict;
use warnings;

my %header = ("X-Test-Header" => "XOriginalValue");
our %args = (
    client => {
	func => \&http_client,
	loggrep => {
		qr/X-Test-Header: XChangedValue/ => 1,
		qr/Host: foo.bar/ => 1,
	}
    },
    relayd => {
	protocol => [ "http",
	    'match request header set "Host" value "foobar.changed"',
	    'match response header set "X-Test-Header" value "XChangedValue"',
	],
    },
    server => {
	func => \&http_server,
	header => \%header,
	loggrep => {
		qr/X-Test-Header: XOriginalValue/ => 1,
		qr/Host: foobar.changed/ => 1,
	},
    },
);

1;
