# test waiting for splice finish with blocking read

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    relay => {
	readblocking => 1,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
