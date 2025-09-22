#	$OpenBSD: Default.pm,v 1.3 2016/09/28 12:40:35 bluhm Exp $

# Copyright (c) 2010-2014 Alexander Bluhm <bluhm@openbsd.org>
# Copyright (c) 2014 Florian Riehm <mail@friehm.de>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use strict;
use warnings;

package Default;
use parent qw( Exporter );
our @ISA = qw( Exporter );

our @EXPORT = qw(
    $area
    $tap_number
    $ospfd_ip
    $ospfd_rtrid
    %default_args
);

our $area = "10.188.0.0";
our $tap_number = $ENV{TAPNUM};
our $ospfd_ip = $ENV{TAPIP};
our $ospfd_rtrid = $ENV{RTRID};

my $hello_interval = 2;
our %default_args = (
    ospfd => {
	configtest => 0,
	conf => {
	    global => {
		'router-id' => $ospfd_rtrid,
	    },
	    areas => {
		$area => {
		    "tap$tap_number:$ospfd_ip" => {
			'metric' => '15',
			'hello-interval' => $hello_interval,
			'router-dead-time' => 4 * $hello_interval,
			'router-priority' => '15',
		    },
		},
	    },
	},
    },
    client => {
	mac_address => "2:3:4:5:6:7",
	ospf_address => "10.188.6.18",
	router_id => "10.188.0.18",
	area => $area,
	hello_intervall => $hello_interval,
	tap_number => $tap_number,
	ospfd_ip => $ospfd_ip,
	ospfd_rtrid => $ospfd_rtrid,
    },
);

1;
