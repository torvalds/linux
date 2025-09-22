# The client writes a message to a localhost IPv4 UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd has both any UDP sockets in fstat output.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, addr => "127.0.0.1", port => 514 },
    },
    syslogd => {
	fstat => {
	    qr/ internet dgram udp \*:514$/ => 1,
	    qr/ internet6 dgram udp \*:514$/ => 1,
	},
	options => ["-nu"],
    },
);

1;
