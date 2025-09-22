# The client writes a message to Sys::Syslog native method.
# The syslogd writes it into a file and through a pipe.
# The syslogd fails to to do DNS lookup of UDP loghost.
# Find the message in client, file, pipe, syslogd log.
# Check that syslogd log contains dns failures and retry.

use strict;
use warnings;
use Socket;

our %args = (
    syslogd => {
	loghost => '@udp://noexist.invalid.:514',
	loggrep => {
	    qr/bad hostname "\@udp:\/\/noexist.invalid.:514"/ => '>=2',
	    qr/Logging to FORWUDP \@udp:\/\/noexist.invalid.:514/ => '>=4',
	    qr/retry loghost "\@udp:\/\/noexist.invalid.:514" wait 1/ => 1,
	    get_testgrep() => 1,
	},
    },
    server => {
	noserver => 1,
    },
);

1;
