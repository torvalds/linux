# The syslogd has a non existing log file in its config.
# The client waits for syslogd startup and sends sighup.
# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that only the error message is twice in the console log.

use strict;
use warnings;

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    ${$self->{syslogd}}->loggrep("syslogd: started", 5)
		or die ref($self), " no 'syslogd: started' in log";
	    ${$self->{syslogd}}->kill_syslogd('HUP');
	    ${$self->{syslogd}}->loggrep("syslogd: restarted", 5)
		or die ref($self), " no 'syslogd: restarted' in log";
	    write_log($self);
	},
    },
    syslogd => {
	conf => "*.*\t\$objdir/file-noexist.log\n",
	loggrep => {
	    qr{syslogd\[\d+\]: priv_open_log ".*/file-noexist.log": }.
		qr{No such file or directory} => 2,
	    qr/syslogd: started/ => 1,
	    qr/syslogd: restarted/ => 1,
	},
	noconsole => 1,  # do not write /dev/console in config file
    },
    console => {
	loggrep => {
	    qr{".*/file-noexist.log": No such file or directory} => 2,
	    qr/syslogd\[\d+\]: start/ => 0,
	    qr/syslogd\[\d+\]: restart/ => 0,
	    get_testgrep() => 0,
	},
    },
    file => {
	loggrep => {
	    qr{".*/file-noexist.log": No such file or directory} => 0,
	    qr/syslogd\[\d+\]: start/ => 1,
	    qr/syslogd\[\d+\]: restart/ => 1,
	    get_testgrep() => 1,
	},
    },
);

1;
