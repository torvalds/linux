# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Syslogc lists the memory logs.
# Find the message in client, file, pipe, syslogd, server log.

use strict;
use warnings;

our %args = (
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 1,
	    qr/ctlcmd 4/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogc => {
	options => ["-q"],
	loggrep => qr/^memory /,
    },
);

1;
