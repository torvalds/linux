# The client writes messages with various methods.
# The syslogd writes them into a file and through a pipe and to tty.
# The syslogd is run with -h, adds a hostname, and passes them to loghost.
# The server receives the messages on its UDP socket.
# Find the message in client, file, pipe, console, user, syslogd, server log.
# Check that the hostname in file and server log is correct.

use strict;
use warnings;
use Socket;
use Sys::Hostname;

(my $host = hostname()) =~ s/\..*//;

our %args = (
    client => {
	redo => [
	    { connect => {
		proto  => "udp",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "tcp",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "tls",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 6514,
	    }},
	    { logsock => {
		type  => "native",
	    }},
	    { logsock => {
		type  => "unix",
	    }},
	    { logsock => {
		type  => "udp",
		host => "127.0.0.1",
		port => 514,
	    }},
	    { logsock => {
		type  => "tcp",
		host => "127.0.0.1",
		port => 514,
	    }},
	],
	func => sub { redo_connect(shift, sub {
	    my $self = shift;
	    write_message($self, "client connect proto: ".
		$self->{connectproto}) if $self->{connectproto};
	    write_message($self, "client logsock type: ".
		$self->{logsock}{type}) if $self->{logsock};
	})},
    },
    syslogd => {
	options => [qw(-h -n -rr
	    -U 127.0.0.1:514 -T 127.0.0.1:514 -S 127.0.0.1:6514)],
    },
    server => {
	loggrep => {
	    qr/ client connect / => 3,
	    qr/:\d\d $host client connect proto: udp$/ => 1,
	    qr/:\d\d $host client connect proto: tcp$/ => 1,
	    qr/:\d\d $host client connect proto: tls$/ => 1,
	    qr/ client logsock / => 4,
	    qr/:\d\d $host syslogd-.*: client logsock type: native/ => 1,
	    qr/:\d\d $host syslogd-.*: client logsock type: unix/ => 1,
	    qr/:\d\d $host syslogd-.*: client logsock type: udp/ => 1,
	    qr/:\d\d $host syslogd-.*: client logsock type: tcp/ => 1,
	},
    },
    file => {
	loggrep => {
	    qr/ client connect / => 3,
	    qr/:\d\d 127.0.0.1 client connect proto: udp$/ => 1,
	    qr/:\d\d 127.0.0.1 client connect proto: tcp$/ => 1,
	    qr/:\d\d 127.0.0.1 client connect proto: tls$/ => 1,
	    qr/ client logsock / => 4,
	    qr/:\d\d $host syslogd-.*: client logsock type: native/ => 1,
	    qr/:\d\d $host syslogd-.*: client logsock type: unix/ => 1,
	    qr/:\d\d 127.0.0.1 syslogd-.*: client logsock type: udp/ => 1,
	    qr/:\d\d 127.0.0.1 syslogd-.*: client logsock type: tcp/ => 1,
	},
    },
);

1;
