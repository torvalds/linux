# The client writes a message to Sys::Syslog native method.
# The syslogd writes into duplicate files depending on program and hostname.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the message exaclty appears in the files allowed by both blocks.

use strict;
use warnings;

our %args = (
    syslogd => {
	conf => <<'EOF',
!nonexist
+nonexist
*.*	$objdir/file-0.log
*.*	$objdir/file-1.log
*.*	$objdir/file-2.log
*.*	$objdir/file-3.log
!syslogd-regress
*.*	$objdir/file-1.log
*.*	$objdir/file-2.log
*.*	$objdir/file-4.log
*.*	$objdir/file-5.log
+$host
*.*	$objdir/file-2.log
*.*	$objdir/file-3.log
*.*	$objdir/file-5.log
*.*	$objdir/file-6.log
EOF
    },
    # exactly the files in the last block get the log message
    multifile => [
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 1 } },
	{ loggrep => { get_testgrep() => 1 } },
	{ loggrep => { get_testgrep() => 0 } },
	{ loggrep => { get_testgrep() => 1 } },
	{ loggrep => { get_testgrep() => 1 } },
    ],
);

1;
