# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that a modified config file restarts syslogd child.

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
	    qr/syslogd  RET   execve JUSTRETURN/ => 4,
	},
	loggrep => {
	    qr/config file modified: restarting/ => 1,
	    qr/config file changed: dying/ => 1,
	    qr/syslogd: restarted/ => 0,
	    get_between2loggrep(),
	},
    },
    server => {
	func => sub { read_between2logs(shift, sub {
	    my $self = shift;
	    my $conffile = ${$self->{syslogd}}->{conffile};
	    open(my $fh, '>>', $conffile)
		or die ref($self), " append conf file $conffile failed: $!";
	    print $fh "# modified\n";
	    close($fh);
	    ${$self->{syslogd}}->kill_syslogd('HUP');
	    ${$self->{syslogd}}->loggrep("syslogd: started", 5, 2)
		or die ref($self), " no 'syslogd: started' between logs";
	    print STDERR "Signal\n";
	})},
	loggrep => { get_between2loggrep() },
    },
    file => {
	loggrep => {
	    qr/syslogd\[\d+\]: start/ => 2,
	    qr/syslogd\[\d+\]: restart/ => 0,
	    qr/syslogd\[\d+\]: exiting/ => 1,
	},
    },
);

1;
