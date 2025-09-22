# The client writes a long message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via TCP to the loghost.
# The server receives the message on its TCP socket.
# Find the message in client, file, syslogd, server log.
# Check that 8192 bytes messages can be processed.

use strict;
use warnings;
use Socket;
use Sys::Hostname;
use constant MAXLINE => 8192;

(my $host = hostname()) =~ s/\..*//;
my $time = "... .. ..:..:..";  # Oct 30 19:10:11
# file entry is without <70> but with space, timestamp and hostname
my $filelen = MAXLINE - 4 + length($time) + 1 + length($host) + 1;

our %args = (
    client => {
	logsock => { type => "native" },
	func => sub {
	    my $self = shift;
	    write_chars($self, MAXLINE);
	    write_shutdown($self);
	},
	loggrep => { get_charlog() => 1 },
    },
    syslogd => {
	loghost => '@tcp://localhost:$connectport',
	loggrep => {
	    qr/[gs]etsockopt bufsize/ => 0,
	    get_charlog() => 1,
	},
    },
    server => {
	listen => { domain => AF_UNSPEC, proto => "tcp", addr => "localhost" },
	# syslog over TCP appends a \n
	loggrep => { qr/^>>> 8209 <70>$time .{8188}\n/ => 1 },
    },
    file => {
	loggrep => { qr/^.{$filelen}\n/ => 1 },
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
