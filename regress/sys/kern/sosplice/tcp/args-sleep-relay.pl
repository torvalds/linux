# test delay before relay copy

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    relay => {
	func => sub { sleep 3; relay(@_); },
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
