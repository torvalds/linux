# test divert-to with icmp
# create a divert-to in rule on the remote machine
# client sends a ICMP echo packet from the local machine
# server receives the ICMP echo packet at the remote machine

use strict;
use warnings;
use Socket;

our %args = (
    socktype => Socket::SOCK_RAW,
    protocol => sub { shift->{af} eq "inet" ? "icmp" : "icmp6" },
    client => { func => \&write_icmp_echo, out => "ICMP6?", noin => 1, },
    server => { func => \&read_icmp_echo, in => "ICMP6?", noout => 1, },
    divert => "to",
);
