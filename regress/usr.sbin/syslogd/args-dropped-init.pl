# syslogd creates and drops some error messages during startup.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that the dropped during initialization is in all log files.
# Check that startup error message is in syslogd stderr.
# Check that initialization error message is in console.

use strict;
use warnings;

our %args = (
    syslogd => {
	options => [qw(-U 0.0.0.0:123456)],
	conf => "*.*\t/nonexistent\n",
	loggrep => {
	    qr/port 123456: service not supported/ => 1,
	    qr/socket bind udp failed/ => 1,
	    qr/"\/nonexistent": No such file or directory/ => 1,
	    qr/dropped 3 messages during initialization/ => 1,
	}
    },
    console => {
	loggrep => {
	    qr/port 123456: service not supported/ => 0,
	    qr/socket bind udp failed/ => 0,
	    qr/"\/nonexistent": No such file or directory/ => 1,
	    qr/dropped 3 messages during initialization/ => 1,
	},
    },
    server => {
	loggrep => {
	    qr/port 123456: service not supported/ => 0,
	    qr/socket bind udp failed/ => 0,
	    qr/"\/nonexistent": No such file or directory/ => 0,
	    qr/dropped 3 messages during initialization/ => 1,
	},
    },
    file => {
	loggrep => {
	    qr/port 123456: service not supported/ => 0,
	    qr/socket bind udp failed/ => 0,
	    qr/"\/nonexistent": No such file or directory/ => 0,
	    qr/dropped 3 messages during initialization/ => 1,
	},
    },
    pipe => {
	loggrep => {
	    qr/dropped 3 messages during initialization/ => 1,
	},
    },
    user => {
	loggrep => {
	    qr/dropped 3 messages during initialization/ => 1,
	},
    },
);

1;
