# The syslogd listens on localhost TLS socket with client verification.
# The client connects with a client certificate and writes a message.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd switches name to common name in certificate.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tls", addr => "127.0.0.1",
	    port => 6514 },
	sslcert => "myhost-client.crt",
	sslkey => "myhost-client.key",
    },
    syslogd => {
	options => ["-S", "127.0.0.1", "-K", "ca.crt"],
    },
    file => {
	loggrep => [
	    qr/localhost using hostname "myhost" from certificate/,
	    qr/ myhost syslogd regress test log message/,
	],
    },
);

1;
