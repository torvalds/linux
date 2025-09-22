# The syslogd listens on localhost TLS socket.
# The client checks that syslogd logs complete handshake.
# The client writes a message into a localhost TLS socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check completed tls handshake log before writing any log message.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_UNSPEC, proto => "tls", addr => "localhost",
	    port => 6514 },
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("Completed tls handshake", 5)
		or die ref($self), " no completed tls handshake syslogd.log";
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-S", "localhost"],
	loggrep => {
	    qr/Accepting tcp connection/ => 1,
	    qr/Completed tls handshake/ => 1,
	},
    },
);

1;
