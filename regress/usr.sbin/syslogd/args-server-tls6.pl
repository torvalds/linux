# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv6 TLS to an explicit loghost.
# The server receives the message on its TLS socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogd and server log contain ::1 address.
# Check that with TLS server all other sockets are closed.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tls://[::1]:$connectport',
	loggrep => {
	    qr/Logging to FORWTLS \@tls:\/\/\[::1\]:\d+/ => '>=4',
	    get_testgrep() => 1,
	},
	fstat => {
	    qr/stream tcp/ => 1,
	    qr/^_syslogd .* internet6 stream tcp / => 1,
	},
    },
    server => {
	listen => { domain => AF_INET6, proto => "tls", addr => "::1" },
	loggrep => {
	    qr/listen sock: ::1 \d+/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
