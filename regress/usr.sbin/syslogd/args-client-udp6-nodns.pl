# The client writes a message to a localhost IPv6 UDP socket.
# The syslogd writes it into a file and through a pipe without dns.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the ::1 address.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET6, addr => "::1", port => 514 },
    },
    syslogd => {
	options => ["-un"],
    },
    file => {
	loggrep => qr/ ::1 /. get_testgrep(),
    },
);

1;
