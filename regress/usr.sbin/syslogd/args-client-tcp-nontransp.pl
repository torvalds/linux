# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes non transparent framing messages into TCP socket.
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
	    print "\na\nbc\nde\r\nfg\000hi \njk\007\nl\n\nm\n \n n\n o \n";
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
	    qr/localhost jk\^G$/ => 1,  # bell character visual
	    qr/localhost l$/ => 1,
	    qr/localhost m$/ => 1,
	    qr/localhost n$/ => 1,  # leading spaces are striped
	    qr/localhost o $/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
