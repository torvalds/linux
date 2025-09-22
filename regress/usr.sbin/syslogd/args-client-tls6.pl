# The syslogd listens on ::1 TLS socket.
# The client writes a message into a ::1 TLS socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the hostname and message.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET6, proto => "tls", addr => "::1",
	    port => 6514 },
	loggrep => {
	    qr/connect sock: ::1 \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogd => {
	options => ["-S", "[::1]:6514"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/ internet6 stream tcp \w+ \[::1\]:6514$/ => 1,
	},
	ktrace => {
	    qr{NAMI  "/etc/ssl/private/\[::1\]:6514.key"} => 1,
	    qr{NAMI  "/etc/ssl/\[::1\]:6514.crt"} => 0,
	    qr{NAMI  "/etc/ssl/private/::1.key"} => 1,
	    qr{NAMI  "/etc/ssl/::1.crt"} => 1,
	},
	loggrep => {
	    qr{Keyfile /etc/ssl/private/::1.key} => 1,
	    qr{Certfile /etc/ssl/::1.crt} => 1,
	},
    },
    file => {
	loggrep => {
	    qr/ localhost /. get_testgrep() => 1,
	},
    },
);

1;
