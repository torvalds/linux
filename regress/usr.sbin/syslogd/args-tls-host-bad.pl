# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to 127.0.0.1 loghost.
# Server certificate is issued for localhost.
# Find the message in client, file, pipe, syslogd log.
# Check that syslogd denies host `127.0.0.1' and server has no message.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tls://127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWTLS \@tls:\/\/127.0.0.1:\d+/ => '>=4',
	    qr/syslogd\[\d+\]: loghost .* connection error: /.
		qr/name `127.0.0.1' not present in server/ => 1,
	    get_testgrep() => 1,
	},
	cacrt => "ca.crt",
    },
    server => {
	listen => { domain => AF_INET, proto => "tls", addr => "127.0.0.1" },
	loggrep => {
	    qr/listen sock: 127.0.0.1 \d+/ => 1,
	    get_testgrep() => 0,
	},
    },
);

1;
