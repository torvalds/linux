# The syslogd listens on 127.0.0.1 TLS socket.
# The TCP client writes cleartext into the TLS connection to syslogd.
# The client connects and closes the connection to syslogd.
# The syslogd writes the error into a file and through a pipe.
# Find the error message in file, syslogd log.
# Check that syslogd writes a log message about the SSL connect error.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 6514 },
	func => sub {
	    my $self = shift;
	    print "Writing cleartext into a TLS connection is a bad idea\n";
	    ${$self->{syslogd}}->loggrep("tls logger .* connection error", 5)
		or die ref($self), " no connection error in syslogd.log";
	},
	loggrep => {
	    qr/connect sock: 127.0.0.1 \d+/ => 1,
	},
    },
    syslogd => {
	options => ["-S", "127.0.0.1:6514"],
	loggrep => {
	    qr/syslogd\[\d+\]: tls logger .* accepted/ => 1,
	    qr/syslogd\[\d+\]: tls logger .* connection error/ => 1,
	},
    },
    server => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("tls logger .* connection error", 5)
		or die ref($self), " no connection error in syslogd.log";
	},
	loggrep => {},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: tls logger .* connection error: /.
		qr/handshake failed: error:.*:SSL routines:/.
		qr/ACCEPT_SR_CLNT_HELLO:tlsv1 alert protocol version/ => 1,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
