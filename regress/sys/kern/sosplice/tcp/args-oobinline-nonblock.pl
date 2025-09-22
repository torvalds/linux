# test inline out-of-band data with non-blocking relay

use strict;
use warnings;

our %args = (
    client => {
	func => \&write_oob,
    },
    relay => {
	oobinline => 1,
	nonblocking => 1,
    },
    server => {
	func => \&read_oob,
	oobinline => 1,
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
