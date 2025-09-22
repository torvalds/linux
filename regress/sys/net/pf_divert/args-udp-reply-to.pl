# test divert-reply with udp with out and in packet
# create a divert-reply out rule on the remote machine
# client sends a UDP packet from the remote machine
# server reflects the UDP packet at the local machine
# client receives the UDP packet at the remote machine

use strict;
use warnings;
use Socket;

our %args = (
    protocol => "udp",
    client => { func => \&write_read_datagram },
    server => { func => \&read_write_datagram },
    divert => "reply",
);
