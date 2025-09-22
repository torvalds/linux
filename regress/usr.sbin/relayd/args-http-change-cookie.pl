use strict;
use warnings;

my $name = "Set-Cookie";
my %header = ("$name" => [ "test=a;", "test=b;" ]);
our %args = (
    client => {
	func => \&http_client,
	loggrep => {
		qr/$name: test=c/ => 1,
	}
    },
    relayd => {
	protocol => [ "http",
	    'match response header set "'.$name.'" value "test=c"',
	],
    },
    server => {
	func => \&http_server,
	header => \%header,
	loggrep => {
		qr/$name: test=a/ => 1
	},
    },
);

1;
