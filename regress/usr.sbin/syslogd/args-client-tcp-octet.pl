# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes octet counting messages into TCP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains all the messages.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    print "0 1 a2 bc3 de\n4 fg\000X3 hi 4 jk\n\n1 l0 1 m1  2  n2 o ";
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
    },
    file => {
	loggrep => {
	    qr/localhost $/ => 3,
	    qr/localhost a$/ => 1,
	    qr/localhost bc$/ => 1,
	    qr/localhost de$/ => 1,
	    qr/localhost fg$/ => 1,  # NUL terminates message
	    qr/localhost hi $/ => 1,
	    qr/localhost jk $/ => 1,  # new line converted to space
	    qr/localhost l$/ => 1,
	    qr/localhost m$/ => 1,
	    qr/localhost n$/ => 1,  # leading spaces are striped
	    qr/localhost o $/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
