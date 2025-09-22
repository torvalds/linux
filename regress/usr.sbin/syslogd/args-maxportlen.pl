# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd does not pass it via a too long port name.
# Find the message in client, file, pipe, syslogd log.
# Check that the syslogd logs the error.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@127.0.0.1:'.('X'x32),
	loggrep => {
	    qr/syslogd\[\d+\]: port too long "\@127.0.0.1:X+/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	noserver => 1,
    },
);

1;
