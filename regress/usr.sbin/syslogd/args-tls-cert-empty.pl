# Syslogd gets an empty TLS server certificate.
# The client cannot connect to 127.0.0.1 TLS socket.
# Check that syslog log contains an error message.

use strict;
use warnings;
use Socket;

my $cert = "/etc/ssl/127.0.0.1.crt";
my @sudo = $ENV{SUDO} ? $ENV{SUDO} : ();
my @cmd = (@sudo, "cp", "--", "empty", $cert);
system(@cmd) and die "Command '@cmd' failed: $?";
END {
    local $?;
    my @cmd = (@sudo, "cp", "--", "127.0.0.1.crt", $cert);
    system(@cmd) and warn "Command '@cmd' failed: $?";
}

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    IO::Socket::IP->new(
		Domain              => AF_INET,
		Proto               => "tcp",
		PeerAddr            => "127.0.0.1",
		PeerPort            => 6514,
	    ) and die "tcp socket connect to 127.0.0.1:6514 succeeded";
	},
	nocheck => 1,
    },
    syslogd => {
	options => ["-S", "127.0.0.1:6514"],
	ktrace => {
	    qr{NAMI  "/etc/ssl/private/127.0.0.1:6514.key"} => 1,
	    qr{NAMI  "/etc/ssl/127.0.0.1:6514.crt"} => 0,
	    qr{NAMI  "/etc/ssl/private/127.0.0.1.key"} => 1,
	    qr{NAMI  "/etc/ssl/127.0.0.1.crt"} => 1,
	},
	loggrep => {
	    qr{Keyfile /etc/ssl/private/127.0.0.1.key} => 1,
	    qr{Certfile } => 0,
	    qr{tls_configure server: failed to load certificate} => 1,
	},
    },
    server => {
	noserver => 1,
    },
    file => { nocheck => 1 },
    pipe => { nocheck => 1 },
    tty => { nocheck => 1 },
);

1;
