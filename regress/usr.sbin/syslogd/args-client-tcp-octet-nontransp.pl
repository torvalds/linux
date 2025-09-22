# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes octet counting and non transpatent framing in chunks.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains all the messages.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    local $| = 1;
	    print "2 ab";
	    ${$self->{syslogd}}->loggrep("octet counting 2", 5, 1)
		or die ref($self), " syslogd did not 1 octet counting";
	    print "2 c";
	    ${$self->{syslogd}}->loggrep("octet counting 2", 5, 2)
		or die ref($self), " syslogd did not 2 octet counting";
	    print "def\n";
	    ${$self->{syslogd}}->loggrep("non transparent framing", 5, 1)
		or die ref($self), " syslogd did not 1 non transparent framing";
	    print "g";
	    ${$self->{syslogd}}->loggrep("non transparent framing", 5, 2)
		or die ref($self), " syslogd did not 2 non transparent framing";
	    print "h\nij\n2 kl";
	    ${$self->{syslogd}}->loggrep("octet counting 2", 5, 4)
		or die ref($self), " syslogd did not 4 octet counting";
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    qr/tcp logger .* octet counting 2, use 2 bytes/ => 3,
	    qr/tcp logger .* octet counting 2, incomplete frame, /.
		qr/buffer 3 bytes/ => 1,
	    qr/tcp logger .* non transparent framing, use 3 bytes/ => 3,
	    qr/tcp logger .* non transparent framing, incomplete frame, /.
		qr/buffer 1 bytes/ => 1,
	    qr/tcp logger .* use 0 bytes/ => 0,
	    qr/tcp logger .* unknown method/ => 0,
	}
    },
    file => {
	loggrep => {
	    qr/localhost ab$/ => 1,
	    qr/localhost cd$/ => 1,
	    qr/localhost ef$/ => 1,
	    qr/localhost gh$/ => 1,
	    qr/localhost ij$/ => 1,
	    qr/localhost kl$/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
