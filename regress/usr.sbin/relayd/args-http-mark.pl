use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	path => "foobar?path",
    },
    relayd => {
	protocol => [ "http",
	    'match request path "/foobar" value "*" tag RING0',
	    'block request',
	    'pass request quick tagged RING0',
	],
	loggrep => { ", RING0,.*done" => 1 },
    },
    server => {
	func => \&http_server,
    },
    len => 12,
);

1;
