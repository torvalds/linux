# The syslogd listens on localhost TLS socket.
# The client writes a message into a localhost TLS socket.
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
	connect => { domain => AF_UNSPEC, proto => "tls", addr => "localhost",
	    port => 6514 },
	loggrep => {
	    qr/connect sock: (127.0.0.1|::1) \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogd => {
	options => ["-S", "localhost"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/ internet6? stream tcp \w+ (127.0.0.1|\[::1\]):6514$/ => 1,
	},
	ktrace => {
	    qr{NAMI  "/etc/ssl/private/localhost.key"} => 1,
	    qr{NAMI  "/etc/ssl/localhost.crt"} => 1,
	},
	loggrep => {
	    qr{Keyfile /etc/ssl/private/localhost.key} => 1,
	    qr{Certfile /etc/ssl/localhost.crt} => 1,
	    qr/Accepting tcp connection/ => 1,
	    qr/syslogd\[\d+\]: tls logger .* accepted/ => 1,
	    qr/Completed tls handshake/ => 1,
	    qr/syslogd\[\d+\]: tls logger .* connection close/ => 1,
	},
    },
    file => {
	loggrep => {
	    qr/ localhost /. get_testgrep() => 1,
	},
    },
);

1;
