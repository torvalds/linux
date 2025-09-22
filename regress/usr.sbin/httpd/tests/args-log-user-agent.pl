use strict;
use warnings;

our %args = (
    client => {
	header => {
		"User-Agent" => "regress\t\n\nGET / HTTP/1.0\r\n"
	}
    },
    httpd => {
	loggrep => {
	    qr/\"regress\\t\\n\\nGET \/ HTTP\/1\.0\"/ => 1,
	},
    },
);

1;
