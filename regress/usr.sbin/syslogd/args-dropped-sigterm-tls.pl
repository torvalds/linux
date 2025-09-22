# The client writes 300 messages to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TLS to the loghost.
# The server blocks the message on its TLS socket.
# The server waits until the client as written all messages.
# The server sends a SIGTERM to syslogd and reads messages from kernel.
# The client waits until the server has read the first message.
# Find the message in client, file, pipe, syslogd log.
# Check that the 300 messages are in syslogd and file log.
# Check that the dropped message is in file log.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    write_message($self, get_secondlog());
	    write_lines($self, 300, 1024);
	    write_message($self, get_thirdlog());
	    ${$self->{server}}->loggrep(get_secondlog(), 8)
		or die ref($self), " server did not receive second log";
	})},
    },
    syslogd => {
	loghost => '@tls://localhost:$connectport',
	loggrep => {
	    get_charlog() => 300,
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
	    ${$self->{syslogd}}->kill_syslogd('TERM');
	    ${$self->{syslogd}}->loggrep("syslogd: exited", 5)
		or die ref($self), " no 'syslogd: exited' in syslogd log";
	    # syslogd has shut down, read from kernel socket buffer
	    read_log($self);
	},
	loggrep => {
	    get_firstlog() => 1,
	    get_secondlog() => 1,
	    get_thirdlog() => 0,
	    get_testgrep() => 0,
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_charlog() => '~91',
	},
    },
    file => {
	loggrep => {
	    get_firstlog() => 1,
	    get_secondlog() => 1,
	    get_thirdlog() => 1,
	    get_testgrep() => 0,
	    qr/syslogd\[\d+\]: start/ => 1,
	    get_charlog() => 300,
	    qr/syslogd\[\d+\]: dropped 2[0-2][0-9] messages to remote loghost/
		=> 1,
	},
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
