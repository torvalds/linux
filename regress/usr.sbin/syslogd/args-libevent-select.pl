# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check in log and ktrace that select has been used.

use strict;
use warnings;

$ENV{EVENT_NOKQUEUE} = 1;
$ENV{EVENT_NOPOLL} = 1;
$ENV{EVENT_NOSELECT} = 0;

our %args = (
    syslogd => {
	loggrep => qr/libevent using: select/,
	ktrace => qr/CALL  select/,
    },
);

1;
