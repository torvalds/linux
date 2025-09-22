# Start syslogd with relative path.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, console, user, syslogd, server log.
# Check fstat for root and working directory.
# Check ktrace for chroot, chdir, exec.

use strict;
use warnings;

our %args = (
    syslogd => {
	chdir => "/usr/sbin",
	execfile => $ENV{SYSLOGD} ? "../../$ENV{SYSLOGD}" : "./syslogd",
	nopipe => 1,
	fstat => {
	    qr/^root .* wd \/ / => 1,
	    qr/^root .* root / => 0,
	    qr/^_syslogd .* wd / => 1,
	    qr/^_syslogd .* root / => 1,
	},
	ktrace => {
	    qr/CALL  chroot/ => 1,
	    qr/CALL  chdir/ => 2,
	    qr/CALL  exec/ => 2,
	},
    },
    pipe => { nocheck => 1 },
);

1;
