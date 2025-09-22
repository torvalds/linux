# test client ssl certificate verification

use strict;
use warnings;

our %args = (
    client => {
	ssl => 1,
	offertlscert => 0,
	# no-op func as we cannot connect without presenting a client certificate,
	# hence the default write_char function won't work here and block forever.
	func => sub {
		errignore();
		sleep(2);
	},
	dryrun => 1,
	nocheck => 1,
    },
    relayd => {
	listenssl => 1,
	verifyclient => 1,
	loggrep => {
		qr/peer did not return a certificate/ => 1,
		qr/tls session \d+ established/ => 0,
	},
    },
    server => {
	noserver => 1,
	nocheck => 1,
    },
);

1;
