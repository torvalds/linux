# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS with client certificate to the loghost.
# The server verifies the connection to its TLS socket and gets the message.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd has client cert and key in log.
# Check that server has client certificate subject in log.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	options => [qw(-c client.crt -k client.key)],
	loghost => '@tls://localhost:$connectport',
	loggrep => {
	    qr/ClientCertfile client.crt/ => 1,
	    qr/ClientKeyfile client.key/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tls", addr => "localhost" },
	sslca => "ca.crt",
	loggrep => {
	    qr/ssl subject: /.
		qr{/L=OpenBSD/O=syslogd-regress/OU=client/CN=localhost} => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
