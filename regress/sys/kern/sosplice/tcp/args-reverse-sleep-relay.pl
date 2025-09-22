# test delay before relay copy when reverse sending

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_stream,
    },
    relay => {
	func => sub { ioflip(@_); sleep 3; relay(@_); },
    },
    server => {
	func => \&write_stream,
	len => 2**17,
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
