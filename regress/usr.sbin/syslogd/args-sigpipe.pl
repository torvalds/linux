# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that a SIGPIPE is ignored by syslogd.

use strict;
use warnings;

our %args = (
    client => {
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    ${$self->{server}}->loggrep("Signal", 8)
		or die ref($self), " no 'Signal' between logs";
	})},
	loggrep => { get_between2loggrep() },
    },
    syslogd => {
	ktrace => {
	    qr/syslogd  PSIG  SIGPIPE/ => 0,
	    qr/syslogd  RET   execve JUSTRETURN/ => 2,
	},
	loggrep => { get_between2loggrep() },
    },
    server => {
	func => sub { read_between2logs(shift, sub {
	    my $self = shift;
	    ${$self->{syslogd}}->kill_syslogd('PIPE');
	    sleep 1;  # schedule syslogd
	    print STDERR "Signal\n";
	})},
	loggrep => { get_between2loggrep() },
    },
    file => { loggrep => { get_between2loggrep() } },
    pipe => { loggrep => { get_between2loggrep() } },
);

1;
