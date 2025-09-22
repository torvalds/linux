# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Syslogc reads and clears the memory logs.
# Find the message in client, file, pipe, syslogd, server, syslogc log.
# Check that memory buffer has been cleared.

use strict;
use warnings;

our %args = (
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 2,
	    qr/ctlcmd 2/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogc => [ {
	options => ["-c", "memory"],
	down => get_downlog(),
    }, {
	options => ["memory"],
	loggrep => { get_testgrep() => 0 },
    } ],
);

1;
