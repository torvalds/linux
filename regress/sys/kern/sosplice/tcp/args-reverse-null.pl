# test emtpy server write when reverse sending

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_stream,
    },
    relay => {
	func => sub { ioflip(@_); relay(@_); },
    },
    server => {
	func => \&write_stream,
	len => 0,
    },
    len => 0,
    md5 => "d41d8cd98f00b204e9800998ecf8427e",
);
