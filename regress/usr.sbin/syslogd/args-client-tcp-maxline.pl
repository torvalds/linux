# The syslogd listens on 127.0.0.1 TCP socket.
# The client writes long line into a 127.0.0.1 TCP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, syslogd, server log.
# Check that the file log contains the truncated message.

use strict;
use warnings;
use Socket;
use constant MAXLINE => 8192;
use constant MAX_UDPMSG => 1180;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    local $| = 1;
	    my $msg = generate_chars(5+1+MAXLINE+1);
	    print $msg;
	    print STDERR "<<< $msg\n";
	    ${$self->{syslogd}}->loggrep("tcp logger .* incomplete", 5, 1)
		or die ref($self), " syslogd did not receive 1 incomplete";
	    $msg = generate_chars(5+1+MAXLINE);
	    print $msg;
	    print STDERR "<<< $msg\n";
	    ${$self->{syslogd}}->loggrep("tcp logger .* incomplete", 5, 2)
		or die ref($self), " syslogd did not receive 2 incomplete";
	    print "\n";
	    print STDERR "<<< \n";
	    write_shutdown($self);
	},
	loggrep => {
	    qr/<<< 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/ => 2,
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	loggrep => {
	    qr/incomplete frame, use /.(MAXLINE+7).qr/ bytes/ => 1,
	    qr/non transparent framing, use /.(MAXLINE+7).qr/ bytes/ => 1,
	}
    },
    server => {
	# >>> <13>Jul  6 22:33:32 0123456789ABC...fgh
	loggrep => {
	    qr/>>> .{19} /.generate_chars(MAX_UDPMSG-20).qr/$/ => 2,
	},
    },
    file => {
	loggrep => {
	    generate_chars(MAXLINE).qr/$/ => 2,
	},
    },
    pipe => { nocheck => 1 },  # XXX syslogd ignore short writes to pipe
    tty => { nocheck => 1 },
);

1;
