# The client writes messages to MAXUNIX unix domain sockets.
# The syslogd -a writes them into a file and through a pipe.
# The syslogd -a passes them via UDP to the loghost.
# The server receives the messages on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the file log contains a message from every socket.
# Check that no error is printed.

use strict;
use warnings;
use Sys::Hostname;
use IO::Socket::UNIX;
use constant MAXUNIX => 21;

(my $host = hostname()) =~ s/\..*//;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    write_unix($self, "/dev/log");
	    foreach (1..(MAXUNIX-1)) {
		write_unix($self, "unix-$_.sock", $_);
	    }
	    ${$self->{syslogd}}->loggrep(get_testgrep(), 5, MAXUNIX)
		or die ref($self), " syslogd did not receive complete line";
	    write_shutdown($self);
	},
    },
    syslogd => {
	options => [ map { ("-a" => "unix-$_.sock") } (1..(MAXUNIX-1)) ],
	loggrep => {
	    qr/out of descriptors/ => 0,
	},
    },
    file => {
	loggrep => {
	    qr/ $host .* unix socket: /.get_testgrep() => MAXUNIX,
	    "/dev/log unix socket" => 1,
	    (map { " $_ unix socket: ".get_testgrep() => 1 } 1..MAXUNIX-1),
	    MAXUNIX." unix socket: ".get_testgrep() => 0,
	},
    },
);

1;
