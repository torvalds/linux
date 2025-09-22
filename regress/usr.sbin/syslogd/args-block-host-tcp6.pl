# The client writes a message in TCP stream to ::1.
# The syslogd writes into multiple files depending on IPv6-Adress.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the message appears in the correct log files.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET6, proto => "tcp", addr => "::1",
	    port => 514 },
    },
    syslogd => {
	options => ["-n", "-T", "[::1]:514"],
	conf => <<'EOF',
+localhost
*.*	$objdir/file-0.log
+127.0.0.1
*.*	$objdir/file-1.log
+::1
*.*	$objdir/file-2.log
+$host
*.*	$objdir/file-3.log
+*
*.*	$objdir/file-4.log
EOF
    },
    multifile => [
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 1 } },
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 1 } },
    ],
);

1;
