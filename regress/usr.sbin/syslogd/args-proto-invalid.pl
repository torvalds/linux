# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd does not pass it via invalid IPv4 UDP to the loghost.
# Find the message in client, file, pipe, syslogd log.
# Check that the syslogd logs the error.

use strict;
use warnings;

our %args = (
    syslogd => {
	loghost => '@invalid://127.0.0.1',
	loggrep => {
	    qr/syslogd\[\d+\]: bad protocol "\@invalid:\/\/127.0.0.1"/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	noserver => 1,
    },
);

1;
