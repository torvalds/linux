# test maximum data length,
# server sleeps before reading

use strict;
use warnings;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**17,
	nocheck => 1,
    },
    relay => {
	max => 32117,
	big => 1,
    },
    server => {
	func => sub { sleep 3; read_stream(@_); },
    },
    len => 32117,
    md5 => "ee338e9693fb2a2ec101bb28935ed123",
    noecho => 1,
);
