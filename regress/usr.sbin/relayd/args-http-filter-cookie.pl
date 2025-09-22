# test http block cookies

use strict;
use warnings;

my @lengths = (1, 2, 3, 4);
my @cookies = ("med=thx; domain=.foo.bar; path=/; expires=Mon, 27-Oct-2014 04:11:56 GMT;", "", "", "");
our %args = (
    client => {
	func => \&http_client,
	loggrep => {
	    qr/Client missing http 1 response/ => 2,
	    qr/Set-Cookie: a\=b\;/ => 6,
	},
	cookies => \@cookies,
	lengths => \@lengths,
    },
    relayd => {
	protocol => [ "http",
	    'block request cookie log "med" value "thx"',
	    'match response cookie append "a" value "b" tag "cookie"',
	    'pass tagged "cookie"',
	],
	loggrep => qr/Forbidden, \[Cookie: med=thx.*/,
    },
    server => {
	func => \&http_server,
    },
    lengths => [2, 3, 4],
);

1;
