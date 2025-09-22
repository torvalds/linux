# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes three lines into a 127.0.0.1 TCP socket in a single chunk.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in file, pipe, syslogd, server log.
# Check that the file log contains all messages.

use strict;
use warnings;
use Socket;

my %threegrep = (
    get_firstlog() => 1,
    get_secondlog() => 1,
    get_thirdlog() => 1,
);

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    my $msg = get_firstlog()."\n".get_secondlog()."\n".get_thirdlog();
	    write_message($self, $msg);
	    ${$self->{syslogd}}->loggrep(get_thirdlog(), 5)
		or die ref($self), " syslogd did not receive third log";
	    write_shutdown($self);
	},
	loggrep => {},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    %threegrep,
	    qr/tcp logger .* non transparent framing, use \d+ bytes/ => 3,
	},
    },
    server => { loggrep => \%threegrep },
    file => { loggrep => \%threegrep },
    pipe => { loggrep => \%threegrep },
    tty => { loggrep => \%threegrep },
);

1;
