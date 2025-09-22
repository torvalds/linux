use strict;
use warnings;

our %args = (
    client => {
	func => \&http_client,
	len => 33,
    },
    relayd => {
	protocol => [ "http",
	    'match request path "/3*" value "*" tag RING0',
	    'match request tagged RING0 tag RINGX',
	],
	loggrep => { ", RINGX,.*done" => 1 },
    },
    server => {
	func => \&http_server,
    },
    len => 33,
);

1;
