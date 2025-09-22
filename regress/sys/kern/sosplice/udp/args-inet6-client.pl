# test ipv6 client

use strict;
use warnings;
use Socket qw(AF_INET AF_INET6);

our %args = (
    client => {
	connectdomain => AF_INET6,
	connectaddr => "::1",
    },
    relay => {
	listendomain => AF_INET6,
	listenaddr => "::1",
	connectdomain => AF_INET,
	connectaddr => "127.0.0.1",
    },
    server => {
	listendomain => AF_INET,
	listenaddr => "127.0.0.1",
    },
    len => 251,
    md5 => "bc3a3f39af35fe5b1687903da2b00c7f",
);
