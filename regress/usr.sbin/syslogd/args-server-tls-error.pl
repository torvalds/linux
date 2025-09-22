# The TLS server aborts the connection to syslogd.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv4 TLS to an explicit loghost.
# The server receives the message on its TLS socket.
# Find the message in client, pipe, syslogd log.
# Check that syslogd writes a log message about the server error.

use strict;
use warnings;
use Socket;
use Errno ':POSIX';

my @errors = (ECONNRESET, EPIPE);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("loghost .* connection error", 5)
		or die ref($self), " no connection error in syslogd.log";
	    write_log($self);
	},
    },
    syslogd => {
	loghost => '@tls://127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWTLS \@tls:\/\/127.0.0.1:\d+/ => '>=4',
	    get_testgrep() => 1,
	    qr/syslogd\[\d+\]: loghost .* connection error/ => 1,
	},
    },
    server => {
	listen => { domain => AF_INET, proto => "tls", addr => "127.0.0.1" },
	func => sub {
	    my $self = shift;
	    setsockopt(STDOUT, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
		or die ref($self), " set socket linger failed: $!";
	},
	loggrep => {},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: loghost .* connection error: /.
		qr/(?:read|write) failed: .*$errors/ => 1,
	},
    },
);

1;
