# test divert-to with udp
# create a divert-to in rule on the remote machine
# client sends a UDP packet from the local machine
# server receives the UDP packet at the remote machine

use strict;
use warnings;

our %args = (
    protocol => "udp",
    client => { func => \&write_datagram, noin => 1, },
    server => { func => \&read_datagram, noout => 1, },
    divert => "to",
);
