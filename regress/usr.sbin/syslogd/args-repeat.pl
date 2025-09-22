# Start syslogd with -r option.
# The client writes messages repeatedly to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that message repeated is not in server or pipe log.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_message($self, "foo");
	    write_message($self, "bar");
	    write_message($self, "bar");
	    write_message($self, "bar");
	    write_message($self, "foo");
	    write_message($self, "bar");
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-r"],
    },
    server => {
	loggrep => {
	    qr/foo/ => 2,
	    qr/bar/ => 4,
	    qr/message repeated/ => 0,
	},
    },
    pipe => {
	loggrep => {
	    qr/foo/ => 2,
	    qr/bar/ => 4,
	    qr/message repeated/ => 0,
	},
    },
    file => {
	loggrep => {
	    qr/foo/ => 2,
	    qr/bar/ => 2,
	    qr/message repeated 2 times/ => 1,
	},
    },
    tty => {
	loggrep => {
	    qr/foo/ => 2,
	    qr/bar/ => 2,
	    qr/message repeated 2 times/ => 1,
	},
    },
);

1;
