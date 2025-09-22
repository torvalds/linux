# test divert-reply with udp
# create a divert-reply out rule on the remote machine
# client sends a UDP packet from the remote machine
# server receives the UDP packet at the local machine

use strict;
use warnings;

our %args = (
    protocol => "udp",
    client => { func => \&write_datagram, noin => 1, },
    server => { func => \&read_datagram, noout => 1, },
    divert => "reply",
);
