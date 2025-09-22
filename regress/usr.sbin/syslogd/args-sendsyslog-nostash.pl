# Run client before starting syslogd.
# The client tries to fill up the kernel stash with invalid messages.
# The client writes one message before and one after syslogd is started.
# Start syslogd, it reads the second message from the log socket.
# Find the log message in file, syslogd, server log.
# Check that the first message got lost.
# Create a ktrace dump of the client and check that sendsyslog(2) has failed.
# Check that kernel did not write sendsyslog(2) error message to log socket.

use strict;
use warnings;
use Errno ':POSIX';
require 'sys/syscall.ph';

use constant LOGSTASH_SIZE => 100;

my $errno = ENOTCONN;
$! = $errno;
my $error = $!;
my $kerngrep = qr/sendsyslog: dropped \d+ messages?/;

our %args = (
    client => {
	early => 1,
	func => sub {
	    my $self = shift;
	    foreach (0..LOGSTASH_SIZE) {
		# bad system call, NULL pointer as message
		syscall(&SYS_sendsyslog, 0, 42, 0) != -1
		    or warn ref($self), " sendsyslog NULL failed: $!";
	    }
	    write_between2logs($self, sub {
		my $self = shift;
		${$self->{syslogd}}->loggrep(qr/syslogd: started/, 5)
		    or die ref($self), " syslogd started not in syslogd.log";
	})},
	ktrace => {
	    qr/CALL  (\(via syscall\) )?sendsyslog\(/ => '>=103',
	    qr/RET   sendsyslog -1 errno $errno / => 102,
	},
	loggrep => {
	    get_firstlog() => 1,
	    qr/Client sendsyslog NULL failed: $error/ => 101,
	    get_testgrep() => 1,
	},
    },
    syslogd => {
	loggrep => {
	    get_firstlog() => 1,
	    qr/msg $kerngrep/ => 0,
	    get_testgrep() => 1,
	},
    },
    server => {
	loggrep => {
	    get_firstlog() => 1,
	    $kerngrep => 0,
	    get_testgrep() => 1,
	},
    },
    file => {
	loggrep => {
	    get_firstlog() => 1,
	    $kerngrep => 0,
	    get_testgrep() => 1,
	},
    },
);

1;
