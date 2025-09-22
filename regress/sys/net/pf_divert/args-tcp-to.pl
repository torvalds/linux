# test divert-to with tcp
# create a divert-to in rule on the remote machine
# client writes into TCP stream and reads from it on the local machine
# server writes into TCP stream and reads from it on the remote machine

use strict;
use warnings;

our %args = (
    protocol => "tcp",
    client => { func => \&write_read_stream },
    server => { func => \&write_read_stream },
    divert => "to",
);
