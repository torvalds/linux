# The client writes a message to Sys::Syslog native method.
# The client writes an additional  message with local5 and err.
# The syslogd writes it into a file and through a pipe and to tty.
# The special message also goes to all users with wall *.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that the special message is in the user's tty log twice.

use strict;
use warnings;
use Sys::Syslog qw(:macros);

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    syslog(LOG_LOCAL5|LOG_ERR, "test message to all users");
	    write_log($self);
	},
    },
    syslogd => {
	conf => "local5.err\t*",
    },
    user => {
	loggrep => {
	    qr/Message from syslogd/ => 1,
	    qr/syslogd-regress.* test message to all users/ => 2,
	},
    },
);

1;
