# Test with rsyslogd as receiver.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the rsyslogd.
# The rsyslogd receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, rsyslogd log.
# Check that the message is in the rsyslogd out file.

use strict;
use warnings;
use Socket;

our %args = (
    rsyslogd => {
	listen => { domain => AF_INET, proto => "udp", addr => "127.0.0.1" },
	loggrep => {
	    qr/imudp.*: /.get_testlog() => 1,
	    qr/Error/ => 0,
	},
    },
);

1;
