#	$OpenBSD: Packet.pm,v 1.3 2015/01/16 17:06:43 bluhm Exp $

# Copyright (c) 2014-2015 Alexander Bluhm <bluhm@openbsd.org>
# Copyright (c) 2015 Florian Riehm <mail@friehm.de>
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

package Packet;
use parent 'Exporter';
use Carp;

our @EXPORT = qw(
    consume_ether
    consume_arp
    consume_ip
    consume_ospf
    consume_hello
    consume_dd
    construct_ether
    construct_arp
    construct_ip
    construct_ospf
    construct_hello
    construct_dd
);

sub ip_checksum {
    my ($msg) = @_;
    my $chk = 0;
    foreach my $short (unpack("n*", $msg."\0")) {
	$chk += $short;
    }
    $chk = ($chk >> 16) + ($chk & 0xffff);
    return(~(($chk >> 16) + $chk) & 0xffff);
}

sub consume_ether {
    my $packet = shift;

    length($$packet) >= 14
	or croak "ether packet too short: ". length($$packet);
    my $ether = substr($$packet, 0, 14, "");
    my %fields;
    @fields{qw(dst src type)} = unpack("a6 a6 n", $ether);
    foreach my $addr (qw(src dst)) {
	$fields{"${addr}_str"} = sprintf("%02x:%02x:%02x:%02x:%02x:%02x",
	    unpack("C6", $fields{$addr}));
    }
    $fields{type_hex} = sprintf("0x%04x", $fields{type});

    return %fields;
}

sub construct_ether {
    my $fields = shift;
    my $subpacket = shift // "";

    foreach my $addr (qw(src dst)) {
	$$fields{$addr} =
	    pack("C6", map { hex $_ } split(/:/, $$fields{"${addr}_str"}));
    }
    my $packet = pack("a6 a6 n", @$fields{qw(dst src type)});

    return $packet. $subpacket;
}

sub consume_arp {
    my $packet = shift;

    length($$packet) >= 28
	or croak "arp packet too short: ". length($$packet);
    my $arp = substr($$packet, 0, 28, "");
    my %fields;
    @fields{qw(hdr sha spa tha tpa)} = unpack("a8 a6 a4 a6 a4", $arp);
    foreach my $addr (qw(sha tha)) {
	$fields{"${addr}_str"} = sprintf("%02x:%02x:%02x:%02x:%02x:%02x",
	    unpack("C6", $fields{$addr}));
    }
    foreach my $addr (qw(spa tpa)) {
	$fields{"${addr}_str"} = join(".", unpack("C4", $fields{$addr}));
    }
    @fields{qw(hrd pro hln pln op)} = unpack("n n C C n", $fields{hdr});

    return %fields;
}

sub construct_arp {
    my $fields = shift;
    my $subpacket = shift // "";

    foreach my $addr (qw(sha tha)) {
	$$fields{$addr} =
	    pack("C6", map { hex $_ } split(/:/, $$fields{"${addr}_str"}));
    }
    foreach my $addr (qw(spa tpa)) {
	$$fields{$addr} = pack("C4", split(/\./, $$fields{"${addr}_str"}));
    }
    $$fields{hdr} = pack("n n C C n", @$fields{qw(hrd pro hln pln op)});
    my $packet = pack("a8 a6 a4 a6 a4", @$fields{qw(hdr sha spa tha tpa)});

    return $packet. $subpacket;
}

sub consume_ip {
    my $packet = shift;

    length($$packet) >= 20 or croak "ip packet too short: ". length($$packet);
    my $ip = substr($$packet, 0, 20, "");
    my %fields;
    @fields{qw(hlv tos len id off ttl p sum src dst)} =
	unpack("C C n n n C C n a4 a4", $ip);
    $fields{hlen} = ($fields{hlv} & 0x0f) << 2;
    $fields{v} = ($fields{hlv} >> 4) & 0x0f;

    $fields{v} == 4 or croak "ip version is not 4: $fields{v}";
    $fields{hlen} >= 20 or croak "ip header length too small: $fields{hlen}";
    if ($fields{hlen} > 20) {
	$fields{options} = substr($$packet, 0, 20 - $fields{hlen}, "");
    }
    foreach my $addr (qw(src dst)) {
	$fields{"${addr}_str"} = join(".", unpack("C4", $fields{$addr}));
    }

    return %fields;
}

sub construct_ip {
    my $fields = shift;
    my $subpacket = shift // "";

    $$fields{options} //= "";

    $$fields{hlen} = 20 + length($$fields{options});
    $$fields{hlen} & 3 and croak "bad ip header length: $$fields{hlen}";
    $$fields{hlen} < 20
	and croak "ip header length too small: $$fields{hlen}";
    ($$fields{hlen} >> 2) > 0x0f
	and croak "ip header length too big: $$fields{hlen}";
    $$fields{v} = 4;
    $$fields{hlv} =
	(($$fields{v} << 4) & 0xf0) | (($$fields{hlen} >> 2) & 0x0f);

    $$fields{len} = $$fields{hlen} + length($subpacket);

    foreach my $addr (qw(src dst)) {
	$$fields{$addr} = pack("C4", split(/\./, $$fields{"${addr}_str"}));
    }
    my $packet = pack("C C n n n C C xx a4 a4",
	@$fields{qw(hlv tos len id off ttl p src dst)});
    $$fields{sum} = ip_checksum($packet);
    substr($packet, 10, 2, pack("n", $$fields{sum}));
    $packet .= pack("a*", $$fields{options});

    return $packet. $subpacket;
}

sub consume_ospf {
    my $packet = shift;

    length($$packet) >= 24 or croak "ospf packet too short: ". length($$packet);
    my $ospf = substr($$packet, 0, 24, "");
    my %fields;
    @fields{qw(version type packet_length router_id area_id checksum autype
	authentication)} =
	unpack("C C n a4 a4 n n a8", $ospf);
    $fields{version} == 2 or croak "ospf version is not 2: $fields{v}";
    foreach my $addr (qw(router_id area_id)) {
	$fields{"${addr}_str"} = join(".", unpack("C4", $fields{$addr}));
    }

    return %fields;
}

sub construct_ospf {
    my $fields = shift;
    my $subpacket = shift // "";

    $$fields{packet_length} = 24 + length($subpacket);
    $$fields{authentication} = "" if $$fields{autype} == 0;

    foreach my $addr (qw(router_id area_id)) {
	if ($$fields{"${addr}_str"}) {
	    $$fields{$addr} = pack("C4", split(/\./, $$fields{"${addr}_str"}));
	}
    }
    my $packet = pack("C C n a4 a4 xx n",
	@$fields{qw(version type packet_length router_id area_id autype)});
    $$fields{checksum} = ip_checksum($packet. $subpacket);
    substr($packet, 12, 2, pack("n", $$fields{checksum}));
    $packet .= pack("a8", $$fields{authentication});

    return $packet. $subpacket;
}

sub consume_hello {
    my $packet = shift;

    length($$packet) >= 20
	or croak "hello packet too short: ". length($$packet);
    my $hello = substr($$packet, 0, 20, "");
    my %fields;
    @fields{qw(network_mask hellointerval options rtr_pri
	routerdeadinterval designated_router backup_designated_router)} =
	unpack("a4 n C C N a4 a4", $hello);
    foreach my $addr (qw(network_mask designated_router
	backup_designated_router)) {
	$fields{"${addr}_str"} = join(".", unpack("C4", $fields{$addr}));
    }
    length($$packet) % 4 and croak "bad neighbor length: ". length($$packet);
    my $n = length($$packet) / 4;
    $fields{neighbors} = [unpack("a4" x $n, $$packet)];
    $$packet = "";
    foreach my $addr (@{$fields{neighbors}}) {
	push @{$fields{neighbors_str}}, join(".", unpack("C4", $addr));
    }

    return %fields;
}

sub consume_dd {
    my $packet = shift;

    length($$packet) >= 8
	or croak "dd packet too short: ". length($$packet);
    my $dd = substr($$packet, 0, 8, "");
    my %fields;
    @fields{qw(interface_mtu options bits dd_sequence_number)} =
	unpack("n C C N", $dd);
    $fields{bits} <= 7
	or croak "All bits except of I-, M- and MS-bit must be zero";

    return %fields;
}

sub construct_hello {
    my $fields = shift;

    $$fields{neighbors_str} //= [];
    $$fields{neighbors} //= [];

    foreach my $addr (qw(network_mask designated_router
	backup_designated_router)) {
	if ($$fields{"${addr}_str"}) {
	    $$fields{$addr} = pack("C4", split(/\./, $$fields{"${addr}_str"}));
	}
    }
    my $packet = pack("a4 n C C N a4 a4",
	@$fields{qw(network_mask hellointerval options rtr_pri
	routerdeadinterval designated_router backup_designated_router)});

    if ($$fields{neighbors_str}) {
	$$fields{neighbors} = [];
    }
    foreach my $str (@{$$fields{neighbors_str}}) {
	push @{$$fields{neighbors}}, pack("C4", split(/\./, $str));
    }
    my $n = @{$$fields{neighbors}};
    $packet .= pack("a4" x $n, @{$$fields{neighbors}});

    return $packet;
}

sub construct_dd {
    my $fields = shift;

    my $packet = pack("n C C N",
	@$fields{qw(interface_mtu options bits dd_sequence_number)});

    return $packet;
}

1;
