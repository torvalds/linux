# test appending headers, both directions

use strict;
use warnings;

my %header_client = (
	"X-Header-Client" => "ABC",
);
my %header_server = (
	"X-Header-Server" => "XYZ",
);
our %args = (
    client => {
	func => \&http_client,
	header => \%header_client,
	loggrep => {
	    "X-Header-Server: XYZ" => 1,
	    "X-Header-Server: xyz" => 1,
	},
    },
    relayd => {
	protocol => [ "http",
	    'match request header append "X-Header-Client" value "abc"',
	    'match response header append "X-Header-Server" value "xyz"',
	    'match request header log "X-Header*"',
	    'match response header log "X-Header*"',
	],
	loggrep => { qr/ (?:done|last write \(done\)), \[X-Header-Client: ABC\]\ GET \{X-Header-Server: XYZ\};/ => 1 },
    },
    server => {
	func => \&http_server,
	header => \%header_server,
	loggrep => {
	    "X-Header-Client: ABC" => 1,
	    "X-Header-Client: abc" => 1,
	},
    },
);

1;
