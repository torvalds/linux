# test divert-reply with icmp and socket reuse
# create a divert-reply out rule on the remote machine
# client sends a ICMP echo packet from the remote machine
# kernel reflects the ICMP echo packet at the local machine
# client receives the ICMP echo reply packet at the remote machine
# client sends a different ICMP echo packet from the remote machine
# kernel reflects the ICMP echo packet at the local machine
# client receives the ICMP echo reply packet at the remote machine

use strict;
use warnings;
use Socket;

our %args = (
    socktype => Socket::SOCK_RAW,
    protocol => sub { shift->{af} eq "inet" ? "icmp" : "icmp6" },
    client => {
	func => sub {
	    my $self = shift;
	    write_icmp_echo($self, $$);
	    read_icmp_echo($self, "reply");
	    write_icmp_echo($self, $$+1);
	    read_icmp_echo($self, "reply");
	},
	out => "ICMP6?",
	in => "ICMP6? reply",
    },
    # no server as our kernel does the icmp reply automatically
    divert => "reply",
);
