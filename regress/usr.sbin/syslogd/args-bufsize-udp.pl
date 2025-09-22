# Stop syslogd.
# The client writes 8 message with 8192 to a localhost IPv6 UDP socket.
# Continue syslogd.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TCP to the loghost.
# The server receives the message on its TCP socket.
# Find the message in client, file, syslogd, server log.
# Check that 8 long UDP messages can be processed at once.

use strict;
use warnings;
use Socket;
use constant MAXLINE => 8192;

our %args = (
    client => {
	connect => { domain => AF_INET6, addr => "::1", port => 514 },
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->kill_syslogd('STOP');
	    write_lines($self, 8, MAXLINE);
	    IO::Handle::flush(\*STDOUT);
	    ${$self->{syslogd}}->kill_syslogd('CONT');
	    ${$self->{server}}->loggrep(get_charlog(), 8)
		or die ref($self), " server did not receive all messages";
	    write_shutdown($self);
	},
	loggrep => { get_charlog() => 8 },
    },
    syslogd => {
	options => ["-un"],
	loghost => '@tcp://localhost:$connectport',
	loggrep => {
	    qr/[gs]etsockopt bufsize/ => 0,
	    get_charlog() => 8,
	},
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tcp", addr => "localhost" },
	loggrep => { get_charlog() => 8 },
    },
    file => {
	loggrep => { get_charlog() => 8 },
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
