#! /usr/bin/perl
# $OpenBSD: template.pl,v 1.5 2017/03/03 21:34:14 bluhm Exp $

# Copyright (c) 2013 Florian Obser <florian@openbsd.org>
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

use IO::Socket::INET;
use Net::Flow;

my $port = 9996;

sub usage
{
	print STDERR "$0 [9|10]\n";
	exit(1);
}

{
	my $id2name = {
		  1 => 'octetDeltaCount',
		  2 => 'packetDeltaCount',
		  4 => 'protocolIdentifier',
		  5 => 'ipClassOfService',
		  7 => 'sourceTransportPort',
		  8 => 'sourceIPv4Address',
		 10 => 'ingressInterface',
		 11 => 'destinationTransportPort',
		 12 => 'destinationIPv4Address',
		 14 => 'egressInterface',
		 21 => 'flowEndSysUpTime',
		 22 => 'flowStartSysUpTime',
		 27 => 'sourceIPv6Address',
		 28 => 'destinationIPv6Address',
		150 => 'flowStartSeconds',
		151 => 'flowEndSeconds',
		152 => 'flowStartMilliseconds',
		153 => 'flowEndMilliseconds',
	};
	sub id2name { return $id2name->{$_[0]} || $_[0]; }
}

if (scalar(@ARGV) != 1 || ($ARGV[0] != 9 && $ARGV[0] != 10)) {
	usage();
}

if (`ifconfig pflow0 2>&1` ne "pflow0: no such interface\n") {
	system('ifconfig', 'pflow0', 'destroy');
}

my $sock = IO::Socket::INET->new( LocalPort =>$port, Proto => 'udp');
my $pid = fork();
if (!defined $pid) {
	die 'cannot fork';
} elsif ( $pid == 0) {
	my ($packet, $header_ref, $template_ref, $flow_ref, $errors_ref);
	$sock->recv($packet,1548);
	($header_ref, $template_ref, $flow_ref, $errors_ref) =
	    Net::Flow::decode(\$packet, $template_ref);
	foreach my $template (@$template_ref) {
		print('Template Id: ', $template->{TemplateId}, "\n");
		foreach my $template_elem (@{$template->{Template}}) {
			print(id2name($template_elem->{Id}), '(',
			    $template_elem->{Length}, ')', "\n");
		}
	}
} else {
	close($sock);
	system('ifconfig', 'pflow0', 'flowsrc', '127.0.0.1', 'flowdst',
	    '127.0.0.1:9996', 'pflowproto', $ARGV[0]);
	waitpid($pid, 0);
	system('ifconfig', 'pflow0', 'destroy');
}
