use strict;
use warnings;

my $len = 512;
my $path = 1048576;
our %args = (
    client => {
	path => $path,
	http_vers => [ "1.1" ],
	code => "206 Partial Content",
	header => {
		"Range" => "bytes=0-511",
	},
	tls => 1
    },
    httpd => {
	listentls => 1
    },
    len => $len,
    md5 => path_md5("$len")
);

1;
