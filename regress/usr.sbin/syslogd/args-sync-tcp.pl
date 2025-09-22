# The client writes 300 long messages to UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd does a TCP reconnect and passes it to loghost.
# The server blocks the message on its TCP socket.
# The server waits until the client has written all messages.
# The server closes the TCP connection and accepts a new one.
# The server receives the messages on its new accepted TCP socket.
# This way the server receives a block of messages that is truncated
# at the beginning and at the end.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the server does not get lines that are cut in the middle.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_UNSPEC, addr => "localhost", port => 514 },
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    write_message($self, get_secondlog());
	    write_lines($self, 300, 2000);
	    write_message($self, get_thirdlog());
	    ${$self->{server}}->loggrep("Accepted", 5, 2)
		or die ref($self), " server did not accept second connection";
	    ${$self->{syslogd}}->loggrep(qr/: dropped \d+ messages? to/, 5)
		or die ref($self), " syslogd did not write dropped message";
	})},
    },
    syslogd => {
	options => ["-u"],
	loghost => '@tcp://127.0.0.1:$connectport',
	loggrep => {
	    get_between2loggrep(),
	    get_charlog() => 300,
	    qr/loghost .* dropped partial message/ => 1,
	},
    },
    server => {
	listen => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1" },
	rcvbuf => 2**12,
	func => sub { accept_between2logs(shift, sub {
	    my $self = shift;
	    # read slowly to get output buffer out of sync
	    foreach (1..10) {
		print STDERR ">>> ". scalar <STDIN>;
		sleep 1;
		last if ${$self->{syslogd}}->loggrep(get_thirdlog());
	    }
	    ${$self->{syslogd}}->loggrep(get_thirdlog(), 30)
		or die ref($self), " syslogd did not receive third log";
	    shutdown(\*STDOUT, 1)
		or die ref($self), " shutdown write failed: $!";
	})},
	loggrep => {
	    qr/^Accepted$/ => 2,
	    get_between2loggrep(),
	    get_thirdlog() => 0,
	},
    },
    file => {
	loggrep => {
	    get_between2loggrep(),
	    get_secondlog() => 1,
	    get_thirdlog() => 1,
	    get_charlog() => 300,
	},
    },
);

1;
