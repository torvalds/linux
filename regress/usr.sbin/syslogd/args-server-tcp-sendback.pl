# The TCP server writes a message back to the syslogd.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via IPv4 TCP to an explicit loghost.
# The server receives the message on its TCP socket.
# Find the message in client, pipe, syslogd, server log.
# Check that syslogd writes a debug message about the message sent back.

use strict;
use warnings;
use Socket;

my $sendback = "syslogd tcp server send back message";

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("loghost .* did send .* back", 5)
		or die ref($self), " no send back in syslogd.log";
	    write_log($self);
	},
    },
    syslogd => {
	loghost => '@tcp://127.0.0.1:$connectport',
	loggrep => {
	    qr/Logging to FORWTCP \@tcp:\/\/127.0.0.1:\d+/ => '>=4',
	    get_testgrep() => 1,
	    qr/did send /.length($sendback).qr/ bytes back/ => 1,
	},
    },
    server => {
	listen => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1" },
	func => sub {
	    print($sendback);
	    read_log(@_);
	},
    },
    file => {
	loggrep => {
	    qr/$sendback/ => 0,
	},
    },
);

1;
