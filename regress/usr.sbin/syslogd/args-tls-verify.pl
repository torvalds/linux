# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to localhost loghost.
# The server receives the message on its TLS socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd and server log contain localhost address.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tls://localhost:$connectport',
	loggrep => {
	    qr/Logging to FORWTLS \@tls:\/\/localhost:\d+/ => '>=4',
	    get_testgrep() => 1,
	    qr/syslogd: loghost .* connection error: connect failed:/ => 0,
	},
	cacrt => "ca.crt",
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tls", addr => "localhost" },
	loggrep => {
	    qr/listen sock: (127.0.0.1|::1) \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
