use strict;
use warnings;

my $len = 1073741824;
our %args = (
    client => {
	tls => 1,
	path => "$len",
	len => $len,
    },
    httpd => {
	listentls => 1,
    },
    len => $len,
    md5 => path_md5("$len"),
);

1;
