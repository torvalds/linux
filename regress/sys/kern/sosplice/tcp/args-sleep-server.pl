# test delay before server read

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    server => {
	func => sub { sleep 3; read_stream(@_); },
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
