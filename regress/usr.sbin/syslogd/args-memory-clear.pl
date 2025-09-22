# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Syslogc clears the memory logs.
# Find the message in client, file, pipe, syslogd, server log.
# Check that memory buffer has been cleared.

use strict;
use warnings;

our %args = (
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 2,
	    qr/ctlcmd 3/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogc => [ {
	options => ["-C", "memory"],
	loggrep => qr/Log cleared/,
    }, {
	options => ["memory"],
	loggrep => { get_testgrep() => 0 },
    } ],
);

1;
