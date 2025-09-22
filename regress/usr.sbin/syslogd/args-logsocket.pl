# The client writes a messages to /dev/log and an alternative log socket.
# The syslogd listens on /var/run/log but not on /dev/log.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that only the meassge to the alternative log socket is logged.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    eval { write_unix($self, "/dev/log") };
	    $@ =~ m,connect to /dev/log unix socket failed,
		or die ref($self), " connect to /dev/log succeeded";
	    write_unix($self, "/var/run/log");
	    ${$self->{syslogd}}->loggrep(get_testgrep(), 2)
		or die ref($self), " syslogd did not receive message";
	    write_shutdown($self);
	},
    },
    syslogd => {
	options => ["-p", "/var/run/log"],
    },
    file => {
	loggrep => {
	    "id /dev/log unix socket" => 0,
	    "id /var/run/log unix socket" => 1,
	},
    },
);

1;
