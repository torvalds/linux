# The syslogd binds UDP socket on 127.0.0.1 and port.
# The client writes a message into a 127.0.0.1 UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains the localhost name.
# Check that fstat contains a bound UDP socket.

use strict;
use warnings;
use Socket;
require 'funcs.pl';

my $port = find_ports(domain => AF_INET, addr => "127.0.0.1");

our %args = (
    client => {
	connect => { domain => AF_INET, addr => "127.0.0.1", port => $port },
    },
    syslogd => {
	options => ["-U", "127.0.0.1:$port"],
	fstat => {
	    qr/^root .* internet/ => 0,
	    qr/ internet dgram udp 127.0.0.1:$port$/ => 1,
	},
    },
    file => {
	loggrep => qr/ localhost /. get_testgrep(),
    },
);

1;
