# test inline out-of-band data when reverse sending

use strict;
use warnings;

our %args = (
    client => {
	func => \&read_oob,
	oobinline => 1,
    },
    relay => {
	func => sub { ioflip(@_); relay(@_); },
	oobinline => 1,
    },
    server => {
	func => \&write_oob,
    },
    len => 251,
    md5 => [
	"24b69642243fee9834bceee5b47078ae",
	"5aa8135a1340e173a7d7a5fa048a999e",
	"e5be513d9d2b877b6841bbb4790c67dc",
	"5cf8c3fd08f541ae07361a11f17213fe",
	"8d509bd55cfabd400403d857386b4956",
    ],
);
