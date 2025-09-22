# Test with default values, that is:
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.

use strict;
use warnings;

our %args = (
);

1;
