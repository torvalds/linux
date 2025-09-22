# Test with rsyslogd as receiver.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to the rsyslogd.
# The rsyslogd receives the message on its TLS socket.
# Find the message in client, file, pipe, syslogd, rsyslogd log.
# Check that the message is in the rsyslogd out file.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@tls://127.0.0.1:$connectport',
	late => 1,  # connect after the listen socket has been created
    },
    rsyslogd => {
	listen => { domain => AF_INET, proto => "tls", addr => "127.0.0.1" },
	loggrep => {
	    qr/omfile.* /.get_testlog() => 1,
	    qr/Error/ => 0,
	    qr/GnuTLS handshake/ => 1,
	},
    },
);

1;
