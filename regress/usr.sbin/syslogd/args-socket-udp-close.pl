# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv4 TCP to an explicit loghost.
# The server receives the message on its TCP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd has closed UDP sockets in fstat output.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tcp://127.0.0.1:$connectport',
	fstat => {
	    qr/ internet dgram udp \*:514$/ => 0,
	    qr/ internet6 dgram udp \*:514$/ => 0,
	},
    },
    server => {
	listen => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1" },
    },
);

1;
