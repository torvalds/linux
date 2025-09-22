# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via default loghost, IPv4, IPv6 to UDP server.
# The server receives the message twice on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the syslogd has IPv4 and IPv6 dgram socket in fstat output.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* internet/ => 2,
	    qr/ internet dgram udp \*:514$/ => 1,
	    qr/ internet6 dgram udp \*:514$/ => 1,
	},
	conf =>
	    "*.*\t\@[127.0.0.1]:\$connectport\n".
	    "*.*\t\@[::1]:\$connectport\n",
    },
    server => {
	loggrep => {
	    get_testgrep() => 2,
	},
    },
);

1;
