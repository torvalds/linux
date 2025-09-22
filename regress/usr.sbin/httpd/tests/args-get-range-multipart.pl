use strict;
use warnings;

my $len = 512;
our %args = (
    client => {
	path => $len,
	http_vers => [ "1.1" ],
	code => "206 Partial Content",
	header => {
		"Range" => "bytes=0-255,256-300,301-",
	},
	multipart => 1
    },
    len => $len,
    md5 => path_md5("$len")
);

1;
