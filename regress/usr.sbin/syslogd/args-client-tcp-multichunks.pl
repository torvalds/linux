# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes a message into a 127.0.0.1 TCP socket in multiple chunks.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in file, pipe, syslogd, server log.
# Check that the file log contains the complete message.

use strict;
use warnings;
use Socket;

my $msglen = length(get_testlog());
my $framelen = $msglen + 1;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    local $| = 1;
	    my $n = 0;
	    foreach (get_testlog() =~ /.{1,5}/g) {
		$n += length;
		print;
		print STDERR "<<< $_\n";
		${$self->{syslogd}}->loggrep("tcp logger .* buffer $n bytes", 5)
		    or die ref($self), " syslogd did not buffer $n bytes";
	    }
	    $n++;
	    print "\n";
	    ${$self->{syslogd}}->loggrep("tcp logger .* use $n bytes", 5)
		or die ref($self), " syslogd did not use $n bytes";
	    write_shutdown($self);
	},
	loggrep => {},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    qr/tcp logger .* buffer \d+ bytes/ => int(($msglen+4)/5),
	    qr/tcp logger .* non transparent framing, use $framelen bytes/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
