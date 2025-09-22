# The syslogd is started with reduced file descriptor limits.
# The syslogd config contains more log files than possible.
# The client connects to the 127.0.0.1 TCP socket, but is not accepted.
# Check the error messages and that syslogd tries to listen again.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	logsock => { type => "tcp", host => "127.0.0.1", port => 514 },
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("Listen again", 5);
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	conf => join("", map { "*.*\t\$objdir/file-$_.log\n" } 0..19),
	rlimit => {
	    RLIMIT_NOFILE => 30,
	},
	loggrep => {
	    qr/syslogd: receive_fd: recvmsg: Message too long/ => 7+1,
	    # One file is opened by test default config, 20 by multifile.
	    qr/X FILE:/ => 1+13,
	    qr/X UNUSED:/ => 7,
	    qr/Accepting tcp connection/ => 0,
	    qr/Listen again/ => '>=1',
	},
    },
    server => {
	loggrep => { get_testlog() => 0 },
    },
    multifile => [
	(map { { loggrep => qr/syslogd\[\d+\]: accept deferred/ } } 0..12),
	(map { { loggrep => { qr/./s => 0 } } } 13..19),
    ],
    file => {
	loggrep => qr/syslogd\[\d+\]: accept deferred: Too many open files/,
    },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
