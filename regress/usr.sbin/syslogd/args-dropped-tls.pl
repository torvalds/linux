# The client writes 400 messages to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to the loghost.
# The server blocks the message on its TLS socket.
# The server waits until the client has written all messages.
# The server receives the message on its TLS socket.
# The client waits until the server as read the first message.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the 400 messages are in syslogd and file log.
# Check that the dropped message is in server and file log.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    write_message($self, get_secondlog());
	    write_lines($self, 400, 1024);
	    write_message($self, get_thirdlog());
	    ${$self->{server}}->loggrep(get_secondlog(), 5)
		or die ref($self), " server did not receive second log";
	    ${$self->{syslogd}}->loggrep(qr/: dropped \d+ messages? to/, 5)
		or die ref($self), " syslogd did not write dropped message";
	})},
    },
    syslogd => {
	loghost => '@tls://localhost:$connectport',
	loggrep => {
	    get_between2loggrep(),
	    get_charlog() => 400,
	    qr/ \(dropped tcpbuf full\)/ => '~65',
	    qr/SSL3_WRITE_PENDING/ => 0,
	},
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tls", addr => "localhost" },
	rcvbuf => 2**12,
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep(get_thirdlog(), 20)
		or die ref($self), " syslogd did not receive third log";
	    read_log($self);
	},
	loggrep => {
	    get_between2loggrep(),
	    get_secondlog() => 1,
	    get_thirdlog() => 0,
	    get_charlog() => '~336',
	    qr/syslogd\[\d+\]: dropped [67][0-9] messages to loghost/ => 1,
	},
    },
    file => {
	loggrep => {
	    get_between2loggrep(),
	    get_secondlog() => 1,
	    get_thirdlog() => 1,
	    get_charlog() => 400,
	    qr/syslogd\[\d+\]: dropped [67][0-9] messages to loghost/ => 1,
	},
    },
);

1;
