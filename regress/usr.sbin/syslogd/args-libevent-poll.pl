# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check in log and ktrace that poll has been used.

use strict;
use warnings;

$ENV{EVENT_NOKQUEUE} = 1;
$ENV{EVENT_NOPOLL} = 0;
$ENV{EVENT_NOSELECT} = 1;

our %args = (
    syslogd => {
	loggrep => qr/libevent using: poll/,
	ktrace => qr/CALL  poll/,
    },
);

1;
