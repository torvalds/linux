# The client connects with TCP.
# The syslogd writes local messages into multiple files depending on priority.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that local syslog messages end up the correct priority file.

use strict;
use warnings;
use Socket;
use Sys::Syslog;

my %selector2messages = (
    "syslog.*"       =>
	[qw{ start .*accepted .*close .*accepted .*peer exiting.* }],
    "syslog.debug"   =>
	[qw{ start .*accepted .*close .*accepted .*peer exiting.* }],
    "syslog.info"    => [qw{ start .*peer exiting.* }],
    "syslog.notice"  => [qw{ .*peer exiting.* }],
    "syslog.warning" => [qw{ exiting.* }],
    "syslog.err"     => [qw{ exiting.* }],
    "syslog.crit"    => [],
    "syslog.alert"   => [],
    "syslog.emerg"   => [],
    "syslog.none"    => [],
);

our %args = (
    client => {
	connect => { domain => AF_INET, proto => "tcp", addr => "127.0.0.1",
	    port => 514 },
	redo => 2,
	func => sub {
	    my $self = shift;
	    $self->{redo}--;
	    if ($self->{redo}) {
		write_message($self, get_testlog());
		IO::Handle::flush(\*STDOUT);
		${$self->{syslogd}}->loggrep(get_testgrep(), 2);
	    } else {
		write_message($self, get_testlog());
		IO::Handle::flush(\*STDOUT);
		${$self->{syslogd}}->loggrep(get_testgrep(), 2, 2);
		setsockopt(STDOUT, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
		    or die ref($self), " set socket linger failed: $!";
		write_shutdown($self);
	    }
	},
    },
    syslogd => {
	options => ["-T", "127.0.0.1:514"],
	conf => selector2config(%selector2messages),
    },
    multifile => [
	(map { { loggrep => $_ } } (selector2loggrep(%selector2messages))),
    ],
);

1;
