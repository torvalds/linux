# The TCP server closes the connection to syslogd.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd does a TCP reconnect and passes it to loghost.
# The server receives the message on its new accepted TCP socket.
# Find the message in client, pipe, syslogd, server log.
# Check that syslogd and server close and reopen the connection.

use strict;
use warnings;
use Socket;
use Errno ':POSIX';

my @errors = (ECONNREFUSED, EPIPE);
my $errors = "(". join("|", map { $! = $_ } @errors). ")";

our %args = (
    client => {
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep($errors, 5)
		or die ref($self), " no $errors in syslogd.log";
	})},
    },
    syslogd => {
	loghost => '@tcp://127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWTCP \@tcp:\/\/127.0.0.1:\d+/ => '>=6',
	    qr/syslogd\[\d+\]: (connect .*|.*connection error): $errors/ => 1,
	    get_between2loggrep(),
	},
    },
    server => {
	listen => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1" },
	func => sub { accept_between2logs(shift, sub {
	    my $self = shift;
	    $self->close();
	    shutdown(\*STDOUT, 1)
		or die ref($self), " shutdown write failed: $!";
	    ${$self->{syslogd}}->loggrep($errors, 5)
		or die ref($self), " no $errors in syslogd.log";
	    $self->listen();
	})},
	loggrep => {
	    qr/^Accepted$/ => 2,
	    qr/syslogd\[\d+\]: loghost .* connection close/ => 1,
	    qr/syslogd\[\d+\]: (connect .*|.*connection error): $errors/ => 1,
	    get_between2loggrep(),
	},
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: (connect .*|.*connection error): $errors/ => 1,
	    get_between2loggrep(),
	},
    },
);

1;
