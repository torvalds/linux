# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes a message to Sys::Syslog TCP method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the hostname and message.

use strict;
use warnings;

our %args = (
    client => {
	logsock => { type => "tcp", host => "127.0.0.1", port => 514 },
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/ internet6? stream tcp \w+ (127.0.0.1|\[::1\]):514$/ => 1,
	},
	loggrep => {
	    qr/syslogd\[\d+\]: tcp logger .* accepted/ => 1,
	    qr/syslogd\[\d+\]: tcp logger .* connection close/ => 1,
	},
    },
    file => {
	loggrep => qr/ localhost syslogd-regress\[\d+\]: /. get_testgrep(),
    },
);

1;
