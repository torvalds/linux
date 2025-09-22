use strict;
use warnings;

my $len = 1048576;
our %args = (
    client => {
	path => "$len",
	len => $len,
	http_vers => [ "1.0" ],
    },
    len => 1048576,
    md5 => path_md5("$len")
);

1;
