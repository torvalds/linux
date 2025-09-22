# The client writes a message with sendsyslog syscall.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Create a ktrace dump of the client and check that sendsyslog(2)
# has been used with flags.

use strict;
use warnings;
use Sys::Syslog 'LOG_CONS';

our %args = (
    client => {
	connect => { domain => "sendsyslog", flags => LOG_CONS },
	ktrace => {
	    qr/CALL  (\(via syscall\) )?sendsyslog\(.*LOG_CONS.*\)/ => 1,
	    qr/GIO   fd -1 wrote \d+ bytes/ => 2,
	    qr/RET   sendsyslog 0/ => 2,
	},
    },
);

1;
