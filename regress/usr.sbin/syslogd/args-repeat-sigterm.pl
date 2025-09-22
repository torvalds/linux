# The client writes messages repeatedly to Sys::Syslog native method.
# Terminate syslogd with unwritten repeated messages.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, syslogd, server log.
# Check that message repeated was written out before syslogd exited.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_message($self, get_testlog());
	    write_message($self, "foobar");
	    write_message($self, "foobar");
	    write_message($self, "foobar");
	    ${$self->{syslogd}}->loggrep(qr/ msg .* foobar/, 5, 3)
		or die ref($self), " syslogd did not receive 3 foobar log";
	    ${$self->{syslogd}}->kill_syslogd('TERM');
	    ${$self->{syslogd}}->loggrep(qr/syslogd: exited/, 8)
		or die ref($self), " syslogd did not exit";
	},
	loggrep => {
	    get_testgrep() => 1,
	    qr/foobar/ => 3,
	},
    },
    syslogd => {
	options => ["-Z"],
	loggrep => {
	    get_testgrep() => 1,
	    qr/logmsg: .* msg .* foobar/ => 3,
	},
    },
    server => {
	down => "exiting on signal 15",
	loggrep => {
	    get_testgrep() => 1,
	    qr/foobar/ => 1,
	    qr/message repeated 2 times/ => 1,
	},
    },
    file => {
	loggrep => {
	    get_testgrep() => 1,
	    qr/foobar/ => 1,
	    qr/message repeated 2 times/ => 1,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
