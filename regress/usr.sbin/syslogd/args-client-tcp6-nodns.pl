# The syslogd listens on ::1 TCP socket.
# The client writes a message into a ::1 TCP socket.
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
	connect => { domain => AF_INET6, proto => "tcp", addr => "::1",
	    port => 514 },
    },
    syslogd => {
	options => ["-n", "-T", "[::1]:514"],
    },
    file => {
	loggrep => qr/ ::1 /. get_testgrep(),
    },
);

1;
