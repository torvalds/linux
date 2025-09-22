# The client writes long messages to UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to the loghost.
# The server receives the message on its TLS socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that lines in server have 8192 bytes message length.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_UNSPEC, addr => "localhost", port => 514 },
	func => sub {
	    my $self = shift;
	    write_lengths($self, 8190..8193,9000);
	    write_log($self);
	},
    },
    syslogd => {
	loghost => '@tls://localhost:$connectport',
	options => ["-u"],
	loggrep => {
	    get_charlog() => 5,
	},
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tls", addr => "localhost" },
	# >>> 8213 <13>Jan 31 00:10:11 0123456789ABC...567\n
	loggrep => {
	    get_charlog() => 5,
	    qr/^>>> 8211 .{19} .{8190}$/ => 1,
	    qr/^>>> 8212 .{19} .{8191}$/ => 1,
	    qr/^>>> 8213 .{19} .{8192}$/ => 3,
	},
    },
);

1;
