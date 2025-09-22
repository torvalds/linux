# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv4 UDP to an explicit loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd and server log contain 127.0.0.1 address.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWUDP \@127.0.0.1:\d+/ => '>=4',
	    get_testgrep() => 1,
	},
    },
    server => {
	listen => { domain => AF_INET, addr => "127.0.0.1" },
	loggrep => {
	    qr/listen sock: 127.0.0.1 \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
