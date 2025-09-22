# test divert-reply with raw ip with out and in packet
# create a divert-reply out rule on the remote machine
# client sends a proto 245 packet from the remote machine
# server reflects the proto 245 packet at the local machine
# client receives the proto 245 packet at the remote machine

use strict;
use warnings;
use Socket;

our %args = (
    socktype => Socket::SOCK_RAW,
    protocol => 254,
    client => { func => \&write_read_datagram },
    server => { func => \&read_write_datagram },
    divert => "reply",
);
