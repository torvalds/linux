# The client writes message with different timestamps to /dev/log.
# The syslogd runs with -Z to translate them to iso time.
# The syslogd writes it into a file and through a pipe and to tty.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check for the correct time conversion in file and server log.

use strict;
use warnings;
use Socket;
use Sys::Hostname;

(my $host = hostname()) =~ s/\..*//;

# 2016-09-28T15:38:09Z
my $iso = qr/20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d\.\d\d\dZ/;
my $bsd = qr/\w\w\w [ \d]\d \d\d:\d\d:\d\d/;

our %args = (
    client => {
	connect => { domain => AF_UNIX },
	func => sub {
	    my $self = shift;
	    write_message($self, "no time");
	    write_message($self, "Oct 11 22:14:15 bsd time");
	    write_message($self, "1985-04-12T23:20:50Z iso time");
	    write_message($self, "1985-04-12T23:20:50.52Z iso frac");
	    write_message($self, "1985-04-12T19:20:50.52-04:00 iso offset");
	    write_message($self, "2003-10-11T22:14:15.003Z iso milisec");
	    write_message($self, "2003-08-24T05:14:15.000003-07:00 iso full");
	    write_message($self, "2003-08-24T05:14:15.000000003-07:00 invalid");
	    write_message($self, "- nil time");
	    write_message($self, "2003-08-24T05:14:15.000003-07:00space");
	    write_message($self, "2003-08-24T05:14:15.-07:00 nofrac");
	    write_message($self, "2003-08-24T05:14:15.1234567-07:00 longfrac");
	    write_message($self, "1985-04-12T23:20:50X zulu");
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-Z"],
    },
    server => {
	loggrep => {
	    qr/>$iso no time$/ => 1,
	    qr/>$iso bsd time$/ => 1,
	    qr/>1985-04-12T23:20:50Z iso time$/ => 1,
	    qr/>1985-04-12T23:20:50.52Z iso frac$/ => 1,
	    qr/>1985-04-12T19:20:50.52-04:00 iso offset$/ => 1,
	    qr/>2003-10-11T22:14:15.003Z iso milisec$/ => 1,
	    qr/>2003-08-24T05:14:15.000003-07:00 iso full$/ => 1,
	    qr/>$iso 2003-08-24T05:14:15.000000003-07:00 invalid$/ => 1,
	    qr/>$iso nil time$/ => 1,
	    qr/>$iso 2003-08-24T05:14:15.000003-07:00space$/ => 1,
	    qr/>$iso 2003-08-24T05:14:15.-07:00 nofrac$/ => 1,
	    qr/>$iso 2003-08-24T05:14:15.1234567-07:00 longfrac$/ => 1,
	    qr/>$iso 1985-04-12T23:20:50X zulu$/ => 1,
	},
    },
    file => {
	loggrep => {
	    qr/^$iso $host no time$/ => 1,
	    qr/^$iso $host bsd time$/ => 1,
	    qr/^1985-04-12T23:20:50Z $host iso time$/ => 1,
	    qr/^1985-04-12T23:20:50.52Z $host iso frac$/ => 1,
	    qr/^1985-04-12T19:20:50.52-04:00 $host iso offset$/ => 1,
	    qr/^2003-10-11T22:14:15.003Z $host iso milisec$/ => 1,
	    qr/^2003-08-24T05:14:15.000003-07:00 $host iso full$/ => 1,
	    qr/^$iso $host 2003-08-24T05:14:15.000000003-07:00 invalid$/ => 1,
	    qr/^$iso $host nil time$/ => 1,
	    qr/^$iso $host 2003-08-24T05:14:15.000003-07:00space$/ => 1,
	    qr/^$iso $host 2003-08-24T05:14:15.-07:00 nofrac$/ => 1,
	    qr/^$iso $host 2003-08-24T05:14:15.1234567-07:00 longfrac$/ => 1,
	    qr/^$iso $host 1985-04-12T23:20:50X zulu$/ => 1,
	},
    },
);

1;
