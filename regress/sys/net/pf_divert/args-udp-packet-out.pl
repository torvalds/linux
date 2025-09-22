# test divert-packet output rule with udp
# create a divert-packet out rule on the remote machine
# client sends a UDP packet from the remote machine
# packet process reflects the UDP packet on divert socket on the remote machine
# server receives the UDP packet at the local machine

use strict;
use warnings;

our %args = (
    protocol => "udp",
    client => {
	func => \&write_datagram,
	noin => 1,
    },
    packet => {
	func => \&read_write_packet,
	in => "Client",
    },
    server => {
	func => \&read_datagram,
	in => "Packet",
	noout => 1,
    },
    divert => "packet-out",
);
