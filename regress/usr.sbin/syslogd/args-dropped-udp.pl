# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP non exising loghost with reject route
# Find dropped udp loghost message in log file.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@udp://127.1.2.3:4567',
	loggrep => {
	    # more messages after 'dropped 4 messages' are dropped
	    qr/Logging to FORWUDP .* \(dropped send error\)/ => '>=4',
	},
    },
    server => {
	noserver => 1,
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: dropped 4 messages to udp loghost/ => 1,
	},
    },
);

1;
