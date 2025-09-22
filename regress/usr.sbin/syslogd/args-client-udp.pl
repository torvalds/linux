# The client writes a message to Sys::Syslog UDP method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the localhost name.

use strict;
use warnings;

our %args = (
    client => {
	logsock => { type => "udp", host => "127.0.0.1", port => 514 },
    },
    syslogd => {
	options => ["-u"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* internet/ => 2,
	},
	loggrep => get_testlog(),
    },
    server => {
	loggrep => get_testlog(),
    },
    pipe => {
	loggrep => get_testlog(),
    },
    tty => {
	loggrep => get_testlog(),
    },
    file => {
	# Sys::Syslog UDP is broken, it appends a \n\0.
	loggrep => qr/ localhost syslogd-regress\[\d+\]: /.get_testlog().qr/ $/,
    },
);

1;
