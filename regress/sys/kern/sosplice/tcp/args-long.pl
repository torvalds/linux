# test longer data length

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
