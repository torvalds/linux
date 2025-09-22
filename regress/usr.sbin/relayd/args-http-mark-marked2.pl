# match and set header with tags

use strict;
use warnings;

my %header_client = (
    "User-Agent" => "Mozilla Bla",
    "MyHeader" => "UnmatchableContent",
);

our %args = (
    client => {
	func => \&http_client,
	header => \%header_client,
	len => 33,
    },
    relayd => {
	protocol => [ "http",
	    # setting the User-Agent should succeed
	    'match request header "User-Agent" value "Mozilla*" tag BORK',
	    'match request header set "User-Agent" value "BORK" tagged BORK',
	    'match request header log "User-Agent"',
	    # setting MyHeader should not happen
	    'match request header "MyHeader" value "SomethingDifferent" tag FOO',
	    'match request header set "MyHeader" value "FOO" tagged FOO',
	    'match request header log "MyHeader"',
	],
	loggrep => {
	    '\[User-Agent: BORK\]' => 1,
	    'MyHeader: FOO' => 0,
	},
    },
    server => {
	func => \&http_server,
	loggrep => {
	    "User-Agent: BORK" => 1,
	    "MyHeader: FOO" => 0,
	}
    },
    len => 33,
);

1;
