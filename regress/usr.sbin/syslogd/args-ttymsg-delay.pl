# The client writes long messages while ttylog to user has been stopped.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, user, syslogd, server log.
# Check that syslogd has logged that the user's tty blocked.

use strict;
use warnings;
use Sys::Syslog qw(:macros);

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->ttykill("user", 'STOP');
	    write_lines($self, 9, 900);
	    ${$self->{syslogd}}->loggrep(qr/ttymsg delayed write/, 3);
	    ${$self->{syslogd}}->ttykill("user", 'CONT');
	    write_log($self);
	},
    },
    syslogd => {
	loggrep => {
	    qr/ttymsg delayed write/ => '>=1',
	},
    },
    user => {
	loggrep => {
	    qr/ 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.* [12]/ => 2,
	    get_testgrep() => 1,
	},
    },
);

1;
