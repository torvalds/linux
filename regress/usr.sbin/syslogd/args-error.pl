# Command line options passed to syslogd generate errors.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that syslogd runs despite of errors.
# Check that startup errors are logged to stderr.

use strict;
use warnings;

our %args = (
    syslogd => {
	cacrt => "default",
	options => [qw(
	    -U 127.188.42.23 -U [::ffff:127.188.0.0] -U 127.0.0.1:70000
	    -T 127.188.42.23:514 -T [::ffff:127.188.0.0]:514 -T 127.0.0.1:70000
	    -S [::1]:70000
	    -C CCCC -c cccc -K KKKK -k kkkk
	    ), '-a', 'A'x1000, '-p', 'P'x1000, '-s', 'S'x1000
	],
	loggrep => {
	    qr/address 127.188.42.23/ => 2,
	    qr/address ::ffff:127.188.0.0/ => 2,
	    qr/port 70000/ => 3,
	    qr/socket bind udp/ => 3,
	    qr/socket listen tcp/ => 3,
	    qr/socket listen tls/ => 1,
	    qr/CA file 'CCCC'/ => 1,
	    qr/certificate file 'cccc'/ => 1,
	    qr/CA file 'KKKK'/ => 1,
	    qr/key file 'kkkk'/ => 1,
	    qr/socket path too long "AAAA/ => 1,
	    qr/socket path too long "PPPP/ => 1,
	    qr/socket path too long "SSSS/ => 1,
	    qr/log socket PPPP/ => 1,
	}
    },
);

1;
