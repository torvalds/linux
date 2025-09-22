# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd -6 does not pass it via IPv4 UDP to the loghost.
# Find the message in client, file, pipe, syslogd log.
# Check that the syslogd logs the error.

use strict;
use warnings;

our %args = (
    syslogd => {
	loghost => '@udp4://127.0.0.1',
	loggrep => {
	    qr/syslogd\[\d+\]: no udp4 "\@udp4:\/\/127.0.0.1/ => 1,
	    get_testgrep() => 1,
	},
	options => ["-6"],
    },
    server => {
	noserver => 1,
    },
);

1;
