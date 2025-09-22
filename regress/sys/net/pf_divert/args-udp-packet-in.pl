# test divert-packet input rule with udp
# create a divert-packet in rule on the remote machine
# client sends a UDP packet from the local machine
# packet process reflects the UDP packet on divert socket on the remote machine
# server receives the UDP packet at the remote machine

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
    divert => "packet-in",
);
