# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via explicit IPv4 UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd log contains the 127.0.0.1 address.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@udp4://127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWUDP \@udp4:\/\/127.0.0.1:\d+/ => '>=4',
	    get_testgrep() => 1,
	},
    },
    server => {
	listen => { domain => AF_INET, addr => "127.0.0.1" },
    },
);

1;
