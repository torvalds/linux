# test divert-reply with tcp
# create a divert-reply out rule on the remote machine
# client writes into TCP stream and reads from it on the remote machine
# server writes into TCP stream and reads from it on the local machine

use strict;
use warnings;

our %args = (
    protocol => "tcp",
    client => { func => \&write_read_stream },
    server => { func => \&write_read_stream },
    divert => "reply",
);
