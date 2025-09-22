# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that there are no bogous new lines in the client log and file.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_message($self, "foo");
	    write_message($self, "bar\n");
	    write_message($self, "foobar\n\n");
	    write_message($self, "");
	    write_message($self, "\n");
	    write_message($self, "\n\n");
	    write_log($self);
	},
	loggrep => {
	    qr/^\s*$/ => 0,
	    qr/[^:] +$/ => 0,
	    qr/: $/ => 3,
	},
    },
    file => {
	loggrep => {
	    qr/^\s*$/ => 0,
	    qr/[^:] +$/ => 0,
	    qr/: $/ => 1,
	},
    },
);

1;
