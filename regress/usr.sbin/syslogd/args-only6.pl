# The client writes a message to a localhost IPv6 UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd -6 passes it via IPv6 UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd has no IPv4 socket in fstat output.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET6, addr => "::1", port => 514 },
    },
    syslogd => {
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* internet/ => 1,
	    qr/ internet / => 0,
	},
	loghost => '@[::1]:$connectport',
	options => ["-6nu"],
    },
    server => {
	listen => { domain => AF_INET6, addr => "::1" },
    },
    file => {
	loggrep => qr/ ::1 /. get_testgrep(),
    },
);

1;
