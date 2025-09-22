# test concurrent read and splice

use strict;
use warnings;
use POSIX;
use Time::HiRes;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**20,
	nocheck => 1,
    },
    relay => {
	# terminate in time on slow machines
	alarm => 25,
	down => "Alarm|Shutdown",
	nonblocking => 1,
	func => sub {
	    defined(my $pid = fork())
		or die "relay func: fork failed: $!";
	    if ($pid == 0) {
		alarm(25);
		my $n;
		do {
		    $n = sysread(STDIN, my $buf, 10);
		} while (!defined($n) || $n);
		POSIX::_exit(0);
	    }
	    # give the userland a moment to read, even if splicing
	    sleep .1;
	    relay(@_);
	    kill 9, $pid;
	    waitpid($pid, 0);
	},
	# As sysread() may extract data from the socket before splicing starts,
	# the spliced content length is not reliable.  Disable the checks.
	nocheck => 1,
    },
    server => {
	func => sub { sleep 2; read_stream(@_); },
	nocheck => 1,
    },
    len => 1048576,
    md5 => '6649bbec13f3d7efaedf01c0cfa54f88',
);
