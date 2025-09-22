# Run client before starting syslogd.
# The client writes one message before and one after syslogd is started.
# The kernel writes a sendsyslog(2) error message to the log socket.
# Start syslogd, it reads the error and the second message from the log socket.
# Find the kernel error message in file, syslogd, server log.
# Check that the first message got lost.
# Create a ktrace dump of the client and check that sendsyslog(2) has failed.

use strict;
use warnings;
use Errno ':POSIX';

use constant LOGSTASH_SIZE => 100;

my $errno = ENOTCONN;
my $kerngrep = qr/sendsyslog: dropped \d+ messages?, error $errno, pid \d+$/;

our %args = (
    client => {
	early => 1,
	func => sub {
	    my $self = shift;
	    write_message($self, "stash $_") foreach (1..LOGSTASH_SIZE);
	    write_between2logs($self, sub {
		my $self = shift;
		${$self->{syslogd}}->loggrep(qr/syslogd: started/, 5)
		    or die ref($self), " syslogd started not in syslogd.log";
	})},
	ktrace => {
	    qr/CALL  sendsyslog\(/ => '>=103',
	    qr/RET   sendsyslog -1 errno $errno / => 101,
	},
    },
    syslogd => {
	loggrep => {
	    get_firstlog() => 0,
	    qr/msg $kerngrep/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_firstlog() => 0,
	    $kerngrep => 1,
	    qr/syslogd\[\d+\]: running/ => 1,
	    get_testgrep() => 1,
	},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_firstlog() => 0,
	    $kerngrep => 1,
	    qr/syslogd\[\d+\]: running/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
