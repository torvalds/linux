# The client writes long messages to UDP socket.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd log.
# Check that lines with visual encoding at the end are truncated.

use strict;
use warnings;
use Socket;

our %args = (
    client => {
	connect => { domain => AF_UNSPEC, addr => "localhost", port => 514 },
	func => sub {
	    my $self = shift;
	    write_lengths($self, [8186..8195,9000], "foo\200"),
	    write_log($self);
	},
    },
    syslogd => {
	options => ["-u"],
	loggrep => {
	    get_charlog() => 11,
	},
    },
    file => {
	# Jan 31 00:12:39 localhost 0123456789ABC...567
	loggrep => {
	    get_charlog() => 11,
	    qr/^.{25} .{8183}fooM\^\@$/ => 1,
	    qr/^.{25} .{8184}fooM\^\@$/ => 1,
	    qr/^.{25} .{8185}fooM\^\@$/ => 1,
	    qr/^.{25} .{8186}fooM\^\@$/ => 1,
	    qr/^.{25} .{8187}fooM\^$/ => 1,
	    qr/^.{25} .{8188}fooM$/ => 1,
	    qr/^.{25} .{8189}foo$/ => 1,
	    qr/^.{25} .{8190}fo$/ => 1,
	    qr/^.{25} .{8191}f$/ => 1,
	    qr/^.{25} .{8192}$/ => 7,
	},
    },
);

1;
