# The syslogd listens on 127.0.0.1 TLS socket with self-signed certificate.
# The client validates cert and writes message into a 127.0.0.1 TLS socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the client file log contains the syslogd certifcate.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_UNSPEC, proto => "tls", addr => "127.0.0.1",
	    port => 6514 },
	loggrep => {
	    qr/connect sock: 127.0.0.1 \d+/ => 1,
	    qr/ssl subject: /.
		qr{/L=OpenBSD/O=syslogd-regress/OU=syslogd/CN=127.0.0.1} => 1,
	    get_testgrep() => 1,
	},
	sslca => "127.0.0.1.crt",
    },
    syslogd => {
	options => ["-S", "127.0.0.1"],
    },
);

1;
