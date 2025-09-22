# The syslogd listens on 127.0.0.1 TCP socket.
# The client connects and aborts the connection to syslogd.
# The syslogd writes the error into a file and through a pipe.
# Find the message in file, syslogd log.
# Check that syslogd writes a log message about the client error.

use strict;
use warnings;
use Socket;
use Errno ':POSIX';

my @errors = (ECONNRESET);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    setsockopt(STDOUT, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
		or die ref($self), " set socket linger failed: $!";
	    ${$self->{syslogd}}->loggrep("tcp logger .* accept", 5)
		or die ref($self), " no accept in syslogd.log";
	},
	loggrep => {
	    qr/connect sock: 127.0.0.1 \d+/ => 1,
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    qr/syslogd\[\d+\]: tcp logger .* accept/ => 1,
	    qr/syslogd\[\d+\]: tcp logger .* connection error/ => 1,
	},
    },
    server => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("tcp logger .* connection error", 5)
		or die ref($self), " no connection error in syslogd.log";
	},
	loggrep => {},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: tcp logger .* connection error: $errors/ => 1,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
