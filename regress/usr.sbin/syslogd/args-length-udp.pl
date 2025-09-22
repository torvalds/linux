# The client writes long messages to UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that lines in server have 1180 bytes line length.
# Check that lines in file have 8192 bytes message length after the header.

use strict;
use warnings;
use Socket;
use constant MAX_UDPMSG => 1180;

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
	options => ["-u"],
	loggrep => {
	    get_charlog() => 5,
	},
    },
    server => {
	# >>> <13>Jan 31 00:10:11 0123456789ABC...lmn
	loggrep => {
	    get_charlog() => 5,
	    qr/^>>> .{19} /.generate_chars(MAX_UDPMSG-20).qr/$/ => 5,
	},
    },
    file => {
	# Jan 31 00:12:39 localhost 0123456789ABC...567
	loggrep => {
	    get_charlog() => 5,
	    qr/^.{25} .{8190}$/ => 1,
	    qr/^.{25} .{8191}$/ => 1,
	    qr/^.{25} .{8192}$/ => 3,
	},
    },
);

1;
