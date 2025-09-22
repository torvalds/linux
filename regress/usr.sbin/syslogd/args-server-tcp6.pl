# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv6 TCP to an explicit loghost.
# The server receives the message on its TCP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd and server log contain ::1 address.
# Check that with TCP server all other sockets are closed.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tcp://[::1]:$connectport',
	loggrep => {
	    qr/Logging to FORWTCP \@tcp:\/\/\[::1\]:\d+/ => '>=4',
	    get_testgrep() => 1,
	},
	fstat => {
	    qr/stream tcp/ => 1,
	    qr/^_syslogd .* internet6 stream tcp / => 1,
	},
    },
    server => {
	listen => { domain => AF_INET6, proto => "tcp", addr => "::1" },
	loggrep => {
	    qr/listen sock: ::1 \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
