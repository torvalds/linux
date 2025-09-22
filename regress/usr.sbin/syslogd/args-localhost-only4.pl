# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd -4 passes it via IPv4 UDP to localhost.
# The server receives the message on its UDP socket.
# Check that localhost gets resolved to the 127.0.0.1 address.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@localhost:$connectport',
	options => ["-4"],
    },
    server => {
	listen => { domain => AF_INET, addr => "127.0.0.1" },
    },
);

1;
