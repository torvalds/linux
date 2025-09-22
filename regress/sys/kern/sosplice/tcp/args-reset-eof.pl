# test connection reset by server at eof, after all data has been read

use strict;
use warnings;

our %args = (
    server => {
	func => sub { read_stream(@_); sleep 3; solingerin(@_); },
    },
);
