# Run client before starting syslogd.
# The client writes 101 messages to kernel log stash.
# The client writes one message before and one after syslogd is started.
# The kernel writes a sendsyslog(2) error message to the log socket.
# Start syslogd, it reads the error and the second message from the log socket.
# Find the kernel error message in file, syslogd, server log.
# Check that the first message got lost.
# Check that at least 80 messages were stashed in kernel.
# Check that the 101th message was lost.

use strict;
use warnings;

use constant LOGSTASH_SIZE => 100;

our %args = (
    client => {
	early => 1,
	func => sub {
	    my $self = shift;
	    write_message($self, "stash $_") foreach (0..LOGSTASH_SIZE);
	    write_between2logs($self, sub {
		my $self = shift;
		${$self->{syslogd}}->loggrep(qr/syslogd: started/, 5)
		    or die ref($self), " syslogd started not in syslogd.log";
	})},
    },
    syslogd => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_firstlog() => 0,
	    qr/syslogd-regress\[\d+\]: stash 0/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 80/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 100/ => 0,
	    qr/sendsyslog: dropped \d+ messages?/ => 1,
	    qr/syslogd\[\d+\]: running/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_firstlog() => 0,
	    qr/syslogd-regress\[\d+\]: stash 0/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 80/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 100/ => 0,
	    qr/sendsyslog: dropped \d+ messages?/ => 1,
	    qr/syslogd\[\d+\]: running/ => 1,
	    get_testgrep() => 1,
	},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_firstlog() => 0,
	    qr/syslogd-regress\[\d+\]: stash 0/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 80/ => 1,
	    qr/syslogd-regress\[\d+\]: stash 100/ => 0,
	    qr/sendsyslog: dropped \d+ messages?/ => 1,
	    qr/syslogd\[\d+\]: running/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
