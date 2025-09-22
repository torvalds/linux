#! /usr/bin/perl
# $OpenBSD: flow.pl,v 1.6 2017/03/03 21:34:14 bluhm Exp $

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
use 5.010;
use Config;

use Data::Dumper;
use IO::Socket::INET;
use Net::Flow;

my $port = 9996;

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
	my $name2id = {reverse %$id2name};
	sub id2name { return $id2name->{$_[0]} || $_[0]; }
	sub name2id { return $name2id->{$_[0]} || $_[0]; }
}

sub get_ifs
{
	my (@ifs, $prog);
	open($prog, 'ifconfig |') or die $!;
	while(<$prog>) {
		chomp;
		push(@ifs, $1) if(/^(\w+):/);
	}
	close($prog) or die $!;
	return(grep({$_ ne 'lo0'} @ifs));
}

sub gen_pf_conf
{
	my @ifs = @_;
	my $skip = 'set skip on {'.join(' ', @ifs).'}';
	return <<END;
$skip
pass on lo0 no state
pass on lo0 proto tcp from port 12345 to port 12346 keep state (pflow)
END
}

if (scalar(@ARGV) != 2 || ($ARGV[0] != 9 && $ARGV[0]!=10)) {
	print STDERR "usage: $0 [9|10] [4|6]\n";
	exit(1);
}

if (scalar(@ARGV) != 2 || ($ARGV[1] != 4 && $ARGV[1]!=6)) {
	print STDERR "usage: $0 [9|10] [4|6]\n";
	exit(1);
}


my @v94_elem_names = qw (sourceIPv4Address
    destinationIPv4Address
    ingressInterface
    egressInterface
    packetDeltaCount
    octetDeltaCount
    flowStartSysUpTime
    flowEndSysUpTime
    sourceTransportPort
    destinationTransportPort
    ipClassOfService
    protocolIdentifier);

my @v96_elem_names = qw (sourceIPv6Address
    destinationIPv6Address
    ingressInterface
    egressInterface
    packetDeltaCount
    octetDeltaCount
    flowStartSysUpTime
    flowEndSysUpTime
    sourceTransportPort
    destinationTransportPort
    ipClassOfService
    protocolIdentifier);

my @v104_elem_names = qw (sourceIPv4Address
    destinationIPv4Address
    ingressInterface
    egressInterface
    packetDeltaCount
    octetDeltaCount
    flowStartMilliseconds
    flowEndMilliseconds
    sourceTransportPort
    destinationTransportPort
    ipClassOfService
    protocolIdentifier);

my @v106_elem_names = qw (sourceIPv6Address
    destinationIPv6Address
    ingressInterface
    egressInterface
    packetDeltaCount
    octetDeltaCount
    flowStartMilliseconds
    flowEndMilliseconds
    sourceTransportPort
    destinationTransportPort
    ipClassOfService
    protocolIdentifier);

my ($name, $sock, $packet, $header_ref, $template_ref, $flow_ref, $flows_ref,
    $error_ref, @elem_names, $prog, $line);

system('ifconfig', 'lo0', 'inet', '10.11.12.13', 'alias');
system('ifconfig', 'lo0', 'inet6', '2001:db8::13');

open($prog, '|pfctl -f -') or die $!;
print $prog gen_pf_conf(get_ifs());
close($prog) or die $!;

if (`ifconfig pflow0 2>&1` ne "pflow0: no such interface\n") {
	system('ifconfig', 'pflow0', 'destroy');
}

system('ifconfig', 'pflow0', 'flowsrc', '127.0.0.1', 'flowdst',
    '127.0.0.1:9996', 'pflowproto', $ARGV[0]);

system('./gen_traffic '.$ARGV[1].' &');

if ($ARGV[0] == 9 && $ARGV[1] == 4) {
	@elem_names = @v94_elem_names;
} elsif ($ARGV[0] == 9 && $ARGV[1] == 6) {
	@elem_names = @v96_elem_names;
} elsif ($ARGV[0] == 10 && $ARGV[1] == 4) {
	@elem_names = @v104_elem_names;
} elsif ($ARGV[0] == 10 && $ARGV[1] == 6) {
	@elem_names = @v106_elem_names;
}

$sock = IO::Socket::INET->new(LocalPort =>$port, Proto => 'udp');
while ($sock->recv($packet,1548)) {
	($header_ref, $template_ref, $flows_ref, $error_ref) =
		Net::Flow::decode(\$packet, $template_ref);
	if (scalar(@$flows_ref) > 0) {
		say scalar(@$flows_ref),' flows';
		foreach $flow_ref (@$flows_ref) {
			say scalar(keys %$flow_ref) - 1, ' elements';
			say 'SetId: ', $flow_ref->{'SetId'};
			my ($iif, $eif, $start, $end);

			my $qpack = $Config{longsize} == 8 ? 'Q>' :
			    $Config{byteorder} == 1234 ? 'L>xxxx' : 'xxxxL>';

			foreach $name (@elem_names) {
				if ($name eq 'ingressInterface') {
					$iif = unpack('N',
					    $flow_ref->{name2id($name)});
				} elsif ($name eq 'egressInterface') {
					$eif = unpack('N',
					    $flow_ref->{name2id($name)});
				} elsif ($name eq 'flowStartSysUpTime') {
					$start = unpack('N',
					    $flow_ref->{name2id($name)})/1000;
				} elsif ($name eq 'flowEndSysUpTime') {
					$end = unpack('N',
					    $flow_ref->{name2id($name)})/1000;
				} elsif ($name eq 'flowStartSeconds') {
					$start = unpack('N',
					    $flow_ref->{name2id($name)});
				} elsif ($name eq 'flowEndSeconds') {
					$end = unpack('N',
					    $flow_ref->{name2id($name)});
				} elsif ($name eq 'flowStartMilliseconds') {
					$start = unpack($qpack,
					    $flow_ref->{name2id($name)})/1000;
				} elsif ($name eq 'flowEndMilliseconds') {
					$end = unpack($qpack,
					    $flow_ref->{name2id($name)})/1000;
				} else {
					say $name,': ', unpack('H*',
					    $flow_ref->{name2id($name)});
				}
			}

			say 'ingressInterface == egressInterface && '.
			    'egressInterface > 0: ', ($iif == $eif && $eif > 0);
		}
		last;
	}
}

END {
	system('ifconfig', 'pflow0', 'destroy');
	system('ifconfig', 'lo0', 'inet', '10.11.12.13', 'delete');
	system('ifconfig', 'lo0', 'inet6', '2001:db8::13', 'delete');
}
