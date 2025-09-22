# Syslogc reads the memory logs continously.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server, syslogc log.
# Check that memory buffer has not been cleared.

use strict;
use warnings;

our %args = (
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 2,
	    qr/ctlcmd 6/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogc => [ {
	early => 1,
	stop => 1,
	options => ["-f", "memory"],
	down => get_downlog(),
    }, {
	options => ["memory"],
	down => get_downlog(),
    } ],
);

1;
