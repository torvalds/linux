# test with non-blocking relay

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    relay => {
	nonblocking => 1,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
