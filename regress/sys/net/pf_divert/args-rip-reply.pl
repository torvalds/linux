# test divert-reply with raw ip
# create a divert-reply out rule on the remote machine
# client sends a proto 254 packet from the remote machine
# server receives the proto 254 packet at the local machine

use strict;
use warnings;
use Socket;

our %args = (
    socktype => Socket::SOCK_RAW,
    protocol => 254,
    client => { func => \&write_datagram, noin => 1, },
    server => { func => \&read_datagram, noout => 1, },
    divert => "reply",
);
