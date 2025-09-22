# test idle timeout, must not be to short

use strict;
use warnings;
use List::Util qw(sum);

my @lengths = (1, 2, 3);

our %args = (
    client => {
	lengths => \@lengths,
	sleep => 1,
    },
    relay => {
	idle => 2,
	timeout => 1,
    },
    len => sum(@lengths),
    lengths => "@lengths",
    md5 => "868972544a6c4312aa52568c8dfa2366",
);
