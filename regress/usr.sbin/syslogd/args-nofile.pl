# The syslogd has a non existing log file in its config.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that the error message during config read is in the console log.

use strict;
use warnings;

our %args = (
    syslogd => {
	conf => "*.*\t\$objdir/file-noexist.log\n",
	loggrep => {
	    qr{syslogd\[\d+\]: priv_open_log ".*/file-noexist.log": }.
		qr{No such file or directory} => 1,
	},
    },
    console => {
	loggrep => {
	    qr{".*/file-noexist.log": No such file or directory} => 1,
	    get_testgrep() => 1,
	},
    },
    file => {
	loggrep => {
	    qr{".*/file-noexist.log": No such file or directory} => 0,
	    get_testgrep() => 1,
	},
    },
);

1;
