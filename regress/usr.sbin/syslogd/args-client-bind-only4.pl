# The syslogd binds UDP socket on localhost with -4.
# The client writes a message into a localhost IPv4 UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the 127.0.0.1 name.
# Check that fstat contains a only bound IPv4 UDP socket.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, addr => "127.0.0.1", port => 514 },
    },
    syslogd => {
	options => ["-4nU", "localhost"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* internet/ => 2,
	    qr/ internet dgram udp 127.0.0.1:514$/ => 1,
	    qr/ internet6 dgram udp \[::1\]:514$/ => 0,
	},
	loghost => '@127.0.0.1:$connectport',
    },
    server => {
	listen => { domain => AF_INET, addr => "127.0.0.1" },
    },
    file => {
	loggrep => qr/ 127.0.0.1 /. get_testgrep(),
    },
);

1;
