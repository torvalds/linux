# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to localhost loghost.
# The cafile is a fake ca with correct DN but wrong key.
# Find the message in client, file, pipe, syslogd log.
# Check that syslogd has verify failure and server has no message.

use strict;
use warnings;
use Errno ':POSIX';
use Socket;

my @errors = (EPIPE);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

our %args = (
    syslogd => {
	loghost => '@tls://localhost:$connectport',
	loggrep => {
	    qr/CAfile fake-ca.crt/ => 1,
	    qr/Logging to FORWTLS \@tls:\/\/localhost:\d+/ => '>=4',
	    qr/syslogd\[\d+\]: loghost .* connection error: /.
		qr/certificate verification failed: /.
		"(".qr/self signed certificate in certificate chain/."|".
		qr/certificate signature failure/.")" => 1,
	    get_testgrep() => 1,
	},
	cacrt => "fake-ca.crt",
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tls", addr => "localhost" },
	up => "IO::Socket::SSL socket accept failed",
	down => "Server",
	exit => 255,
	loggrep => {
	    qr/listen sock: (127.0.0.1|::1) \d+/ => 1,
	    qr/IO::Socket::SSL socket accept failed: /.
		qr/.*,SSL accept attempt failed error:.*/.
		qr/(tlsv1 alert decrypt error|$errors)/ => 1,
	    get_testgrep() => 0,
	},
    },
);

1;
