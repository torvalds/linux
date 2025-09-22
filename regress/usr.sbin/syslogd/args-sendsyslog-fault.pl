# The client fails to use sendsyslog syscall due to bad address.
# The client writes one message wiht sendsyslog.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Create a ktrace dump of the client and check that sendsyslog(2) failed.
# Check that there is no kernel dropped message.

use strict;
use warnings;
use Errno ':POSIX';
require 'sys/syscall.ph';

my $errno = EFAULT;
$! = $errno;
my $error = $!;
my $kerngrep = qr/sendsyslog: dropped \d+ messages?/;

our %args = (
    client => {
	connect => { domain => "sendsyslog" },
	func => sub {
	    my $self = shift;
	    # bad system call, NULL pointer as message
	    syscall(&SYS_sendsyslog, 0, 42, 0) != -1
		or warn ref($self), " sendsyslog NULL failed: $!";
	    write_log($self);
	},
	ktrace => {
	    qr/CALL  (\(via syscall\) )?sendsyslog\(/ => 3,
	    qr/RET   sendsyslog -1 errno $errno / => 1,
	},
	loggrep => {
	    get_firstlog() => 0,
	    qr/Client sendsyslog NULL failed: $error/ => 1,
	    get_testgrep() => 1,
	},
    },
    syslogd => {
	loggrep => {
	    qr/msg $kerngrep/ => 0,
	    get_testgrep() => 1,
	},
    },
    server => {
	loggrep => {
	    $kerngrep => 0,
	    get_testgrep() => 1,
	},
    },
    file => {
	loggrep => {
	    $kerngrep => 0,
	    get_testgrep() => 1,
	},
    },
);

1;
