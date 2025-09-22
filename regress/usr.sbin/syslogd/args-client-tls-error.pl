# The syslogd listens on 127.0.0.1 TLS socket.
# The client connects and aborts the connection to syslogd.
# The syslogd writes the error into a file and through a pipe.
# Find the error message in file, syslogd log.
# Check that syslogd writes a log message about the client error.

use strict;
use warnings;
use Socket;
use Errno ':POSIX';

my @errors = (ECONNRESET);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tls", addr => "127.0.0.1",
	    port => 6514 },
	func => sub {
	    my $self = shift;
	    delete $self->{ts};
	    setsockopt(STDOUT, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
		or die ref($self), " set socket linger failed: $!";
	},
	loggrep => {
	    qr/connect sock: 127.0.0.1 \d+/ => 1,
	},
    },
    syslogd => {
	options => ["-S", "127.0.0.1:6514"],
	loggrep => {
	    qr/syslogd\[\d+\]: tls logger .* accept/ => 1,
	    qr/syslogd\[\d+\]: tls logger .* connection error/ => 1,
	},
    },
    server => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep(
		qr/tls logger .* connection (?:close|error)/, 5)
		or die ref($self), " no connection error in syslogd.log";
	},
	loggrep => {},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: tls logger .* connection error: /.
		qr/read failed: .*$errors/ => 1,
	},
    },
    pipe => { nocheck => 1, },
    tty => { nocheck => 1, },
);

1;
