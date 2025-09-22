# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to localhost loghost.
# The cafile is the system default which has no matching cert.
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
	ktrace => {
	    qr{NAMI  "/etc/ssl/cert.pem"} => 1,
	},
	loggrep => {
	    qr{CAfile /etc/ssl/cert.pem} => 1,
	    qr/Logging to FORWTLS \@tls:\/\/localhost:\d+/ => '>=4',
	    qr/syslogd\[\d+\]: loghost .* connection error: /.
		qr/certificate verification failed: /.
		qr/self signed certificate in certificate chain/ => 1,
	    get_testgrep() => 1,
	},
	cacrt => "default",
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
		qr/(ACCEPT_SR_FINISHED:tlsv1 alert unknown ca|$errors)/ => 1,
	    get_testgrep() => 0,
	},
    },
);

1;
