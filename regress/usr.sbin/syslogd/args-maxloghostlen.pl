# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd does not pass it via a too long loghost name.
# Find the message in client, file, pipe, syslogd log.
# Check that the syslogd logs the error.

use strict;
use warnings;

our %args = (
    syslogd => {
	loghost => '@'.('X'x300),
	loggrep => {
	    qr/syslogd\[\d+\]: loghost too long "\@X+/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	noserver => 1,
    },
);

1;
