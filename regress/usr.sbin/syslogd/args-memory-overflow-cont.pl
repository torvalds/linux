# Syslogc reads the memory logs continously.
# The client writes message to overflow the memory buffer method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Check that syslogc logs lost lines.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_lines($self, 300, 2000);
	    write_log($self);
	},
    },
    syslogd => {
	memory => 1,
	loggrep => {
	    qr/Accepting control connection/ => 1,
	    qr/ctlcmd 6/ => 1,  # read cont
	},
    },
    syslogc => [ {
	early => 1,
	stop => 1,
	options => ["-f", "memory"],
	down => qr/Lines were dropped!/,
	loggrep => {},
    } ],
);

1;
