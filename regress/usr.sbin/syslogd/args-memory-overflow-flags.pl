# The client writes message to overflow the memory buffer method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Syslogc checks the memory logs.
# Find the message in client, file, pipe, syslogd, server log.
# Check that syslogc -o reports overflow.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_lines($self, 40, 2000);
	    write_log($self);
	},
    },
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 1,
	    qr/ctlcmd 5/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogc => {
	options => ["-o", "memory"],
	exit => 1,
	loggrep => {
	    qr/^memory has overflowed/ => 1,
	},
    },
);

1;
