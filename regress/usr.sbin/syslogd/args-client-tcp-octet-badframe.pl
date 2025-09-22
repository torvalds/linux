# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes octet counting messages with invalid framing.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in file, syslogd, server log.
# Check that an invalid octet counting is handled as non transparent framing.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    local $| = 1;
	    print "000002 ab\n";
	    print STDERR "<<< 000002 ab\n";
	    print "2bc\n";
	    print STDERR "<<< 00002bc\n";
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    qr/non transparent framing, use \d+ bytes/ => 3,
	    qr/octet counting/ => 0,
	},
    },
    file => {
	loggrep => {
	    qr/localhost 000002 ab$/ => 1,
	    qr/localhost 2bc$/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
