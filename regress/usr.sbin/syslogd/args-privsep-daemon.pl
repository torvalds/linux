# Start syslogd in daemon mode.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, syslogd, server log.
# Check fstat for the parent and child process.
# Check ktrace for setting the correct uid and gid.
# Check that stdio is dupped to /dev/null.

use strict;
use warnings;

our %args = (
    syslogd => {
	options => ["-u"],
	daemon => 1,
	nopipe => 1,
	noconsole => 1,
	nouser => 1,
	loggrep => {
	    qr/ -F / => 0,
	    qr/ -d / => 0,
	},
	fstat => {
	    qr/^root .* wd / => 1,
	    qr/^root .* root / => 0,
	    qr/^root .* [012] .* null$/ => 3,
	    qr/^root .* kqueue / => 0,
	    qr/^root .* internet/ => 0,
	    qr/^_syslogd .* wd / => 1,
	    qr/^_syslogd .* root / => 1,
	    qr/^_syslogd .* [012] .* null$/ => 3,
	    qr/^_syslogd .* kqueue / => 1,
	    qr/^_syslogd .* internet/ => 2,
	},
	ktrace => {
	    qr/CALL  setresuid(.*"_syslogd".*){3}/ => 1,
	    qr/CALL  setresgid(.*"_syslogd".*){3}/ => 1,
	    qr/CALL  setsid/ => 1,
	    qr/RET   setsid.* errno / => 0,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
