# The client writes a message to a localhost IPv4 UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd -4 passes it via IPv4 UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd has no IPv6 socket in fstat output.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, addr => "127.0.0.1", port => 514 },
    },
    syslogd => {
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* internet/ => 1,
	    qr/ internet6 / => 0,
	},
	loghost => '@127.0.0.1:$connectport',
	options => ["-4nu"],
    },
    server => {
	listen => { domain => AF_INET, addr => "127.0.0.1" },
    },
    file => {
	loggrep => qr/ 127.0.0.1 /. get_testgrep(),
    },
);

1;
