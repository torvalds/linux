# test non idle connection does not timeout by sending a byte every second

use strict;
use warnings;

our %args = (
    client => {
	len => 6,
	sleep => 1,
    },
    relay => {
	idle => 2,
    },
    len => 6,
    md5 => "857f2261690a2305dba03062e778a73b",
);
