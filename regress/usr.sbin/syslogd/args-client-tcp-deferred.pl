# The syslogd listens on 127.0.0.1 TCP socket.
# The client creates connections to syslogd TCP socket until it blocks.
# The client writes to all sockets and closes them.
# Wait until syslogd has slots to accept all sockets.
# Find the message in client, file, pipe, syslogd, server log.
# Check the messages end up in the log file.

use strict;
use warnings;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	func => sub {
	    my $self = shift;
	    local $| = 1;
	    my @s;
	    $s[0] = \*STDOUT;
	    # open additional connections until syslogd deferres
	    for (my $i = 1; $i <= 30; $i++) {
		$s[$i] = IO::Socket::IP->new(
		    Domain              => AF_INET,
		    Proto               => "tcp",
		    PeerAddr            => "127.0.0.1",
		    PeerPort            => 514,
		) or die ref($self), " id $i tcp socket connect failed: $!";
		print STDERR "<<< id $i tcp connected\n";
		${$self->{syslogd}}->loggrep("tcp logger .* accepted", 1, $i);
		${$self->{syslogd}}->loggrep("accept deferred")
		    and last;
	    }
	    write_tcp($self, \*STDOUT, 0);
	    for (my $i = 1; $i < @s; $i++) {
		my $fh = $s[$i];
		write_tcp($self, $fh, $i);
		# close connection so that others can be accepted
		close($fh);
	    }
	    ${$self->{syslogd}}->loggrep(qr/tcp logger .* use \d+ bytes/, 10,
		scalar @s)
		or die ref($self), " syslogd did not use connections";
	    write_shutdown($self);
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	rlimit => {
	    RLIMIT_NOFILE => 30,
	},
	loggrep => {
	    qr/tcp logger .* accepted/ => '>=10',
	    qr/tcp logger .* use \d+ bytes/ => '>=10',
	    qr/tcp logger .* connection close/ => '>=10',
	},
    },
    file => {
	loggrep => {
	    get_testgrep() => '>=10',
	},
    },
);

1;
