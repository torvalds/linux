use strict;
use warnings;

my $len = 1048576;
our %args = (
    client => {
	path => $len,
	http_vers => [ "1.1" ],
	code => "206 Partial Content",
	header => {
		"Range" => "bytes=0-255,256-10240,10241-",
	},
	multipart => 1,
	tls => 1
    },
    httpd => {
	listentls => 1
    },
    len => $len,
    md5 => path_md5("$len")
);

1;
