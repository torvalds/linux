# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, syslogd, server log.
# Check fstat for the parent and child process.
# Check ktrace for setting the correct uid and gid and exec priv.

use strict;
use warnings;

our %args = (
    syslogd => {
	options => ["-u"],
	up => qr/fork\+exec done/,
	nopipe => 1,
	noconsole => 1,
	nouser => 1,
	loggrep => {
	    qr/ -F / => 0,
	    qr/ -d / => '>=1',
	    qr/\[priv\]: fork\+exec done/ => 1,
	},
	fstat => {
	    qr/^root .* wd / => 1,
	    qr/^root .* root / => 0,
	    qr/^root .* kqueue / => 0,
	    qr/^root .* internet/ => 0,
	    qr/^root .* 3\* unix stream/ => 1,
	    qr/^root +syslogd +\d+ +([4-9]|\d\d)/ => 0,
	    qr/^_syslogd .* wd / => 1,
	    qr/^_syslogd .* root / => 1,
	    qr/^_syslogd .* kqueue / => 1,
	    qr/^_syslogd .* internet/ => 2,
	},
	ktrace => {
	    qr/syslogd  CALL  setresuid(.*"_syslogd".*){3}/ => 1,
	    qr/syslogd  CALL  setresgid(.*"_syslogd".*){3}/ => 1,
	    qr/syslogd  CALL  setsid/ => 0,
	    qr/syslogd  RET   execve JUSTRETURN/ => 2,
	    qr/\[\d\] = "-P"/ => 1,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
