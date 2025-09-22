# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that a SIGHUP reopens logfile and restarts pipe.

use strict;
use warnings;

our %args = (
    client => {
	func => sub { write_between2logs(shift, sub {
	    my $self = shift;
	    ${$self->{server}}->loggrep("Signal", 8)
		or die ref($self), " no 'Signal' between logs";
	})},
	loggrep => { get_between2loggrep() },
    },
    syslogd => {
	ktrace => {
	    qr/syslogd  PSIG  SIGHUP caught handler/ => 1,
	    qr/syslogd  RET   execve JUSTRETURN/ => 2,
	},
	loggrep => {
	    qr/config file changed: dying/ => 0,
	    qr/config file modified: restarting/ => 0,
	    qr/syslogd: restarted/ => 1,
	    get_between2loggrep(),
	},
    },
    server => {
	func => sub { read_between2logs(shift, sub {
	    my $self = shift;
	    ${$self->{syslogd}}->rotate();
	    ${$self->{syslogd}}->kill_syslogd('HUP');
	    ${$self->{syslogd}}->loggrep("syslogd: restarted", 5)
		or die ref($self), " no 'syslogd: restarted' between logs";
	    print STDERR "Signal\n";
	})},
	loggrep => {
	    get_between2loggrep(),
	    qr/Signal/ => 1,
	    qr/^Accepted$/ => 1,
	},
    },
    check => sub {
	my $self = shift;
	my $r = $self->{syslogd};
	$r->loggrep("bytes transferred", 1, 2) or sleep 1;
	foreach my $name (qw(file pipe)) {
		my $file = $r->{"out$name"}.".0";
		my $pattern = (get_between2loggrep())[0];
		check_pattern($name, $file, $pattern, \&filegrep);
	}
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 0,
	    qr/syslogd\[\d+\]: restart/ => 1,
	    qr/syslogd\[\d+\]: exiting/ => 1,
	},
    },
);

1;
