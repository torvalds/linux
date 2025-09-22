# The syslogd binds multiple UDP, TCP, TLS sockets on localhost.
# The client writes messages into a all localhost sockets.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the messages on its UDP socket.
# Find the messages in client, file, syslogd log.
# Check that fstat contains all bound sockets.
# Check that the file log contains all messages.
# Check that client used expected protocol.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connectproto => "none",
	redo => [
	    { connect => {
		proto  => "udp",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "udp",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 513,
	    }},
	    { connect => {
		proto  => "udp",
		domain => AF_INET6,
		addr   => "::1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "tcp",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "tcp",
		domain => AF_INET6,
		addr   => "::1",
		port   => 513,
	    }},
	    { connect => {
		proto  => "tcp",
		domain => AF_INET6,
		addr   => "::1",
		port   => 514,
	    }},
	    { connect => {
		proto  => "tls",
		domain => AF_INET6,
		addr   => "::1",
		port   => 6514,
	    }},
	    { connect => {
		proto  => "tls",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 6514,
	    }},
	    { connect => {
		proto  => "tls",
		domain => AF_INET,
		addr   => "127.0.0.1",
		port   => 6515,
	    }},
	],
	func => sub { redo_connect(shift, sub {
	    my $self = shift;
	    write_message($self, "client proto: ". $self->{connectproto});
	})},
	loggrep => {
	    qr/connect sock: (127.0.0.1|::1) \d+/ => 9,
	    get_testgrep() => 1,
	},
    },
    syslogd => {
	options => [qw(-rr
	    -U 127.0.0.1 -U [::1] -U 127.0.0.1:513
	    -T 127.0.0.1:514 -T [::1]:514 -T [::1]:513
	    -S [::1]:6514 -S 127.0.0.1 -S 127.0.0.1:6515
	)],
	fstat => {
	    qr/ internet6? dgram udp (127.0.0.1):513$/ => 1,
	    qr/ internet6? dgram udp (127.0.0.1):514$/ => 1,
	    qr/ internet6? dgram udp (\[::1\]):514$/ => 1,
	    qr/ internet6? stream tcp \w+ (127.0.0.1):514$/ => 1,
	    qr/ internet6? stream tcp \w+ (\[::1\]):513$/ => 1,
	    qr/ internet6? stream tcp \w+ (\[::1\]):514$/ => 1,
	    qr/ internet6? stream tcp \w+ (\[::1\]):6514$/ => 1,
	    qr/ internet6? stream tcp \w+ (127.0.0.1):6514$/ => 1,
	    qr/ internet6? stream tcp \w+ (127.0.0.1):6515$/ => 1,
	},
    },
    file => {
	loggrep => {
	    qr/client proto: udp/ => '>=1',
	    qr/client proto: tcp/ => 3,
	    qr/client proto: tls/ => 3,
	    get_testgrep() => 1,
	}
    },
);

1;
