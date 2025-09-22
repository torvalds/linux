use strict;
use warnings;

my $len = 1073741824;
my @lengths = ($len, $len);
our %args = (
    client => {
	path => "$len",
	http_vers => [ "1.0" ],
	lengths => \@lengths,
    },
    md5 => path_md5("$len"),
    lengths => \@lengths,
);

1;
