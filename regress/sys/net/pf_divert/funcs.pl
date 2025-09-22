#	$OpenBSD: funcs.pl,v 1.6 2017/08/15 04:11:20 bluhm Exp $

# Copyright (c) 2010-2015 Alexander Bluhm <bluhm@openbsd.org>
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
use Socket;
use Socket6;

########################################################################
# Client and Server funcs
########################################################################

sub write_read_stream {
	my $self = shift;

	my $out = ref($self). "\n";
	print $out;
	IO::Handle::flush(\*STDOUT);
	print STDERR ">>> $out";

	my $in = <STDIN>;
	print STDERR "<<< $in";
}

sub write_datagram {
	my $self = shift;
	my $dgram = shift;

	my $out = $dgram || ref($self). "\n";
	my $addr = $self->{toaddr};
	my $port = $self->{toport};
	if ($addr) {
		my ($to, $netaddr);
		if ($self->{af} eq "inet") {
			$netaddr = inet_pton(AF_INET, $addr);
			$to = pack_sockaddr_in($port, $netaddr);
		} else {
			$netaddr = inet_pton(AF_INET6, $addr);
			$to = pack_sockaddr_in6($port, $netaddr);
		}
		$self->{toaddr} = $addr;
		$self->{toport} = $port;
		print STDERR "send to: $addr $port\n";

		send(STDOUT, $out, 0, $to)
		    or die ref($self), " send to failed: $!";
	} else {
		send(STDOUT, $out, 0)
		    or die ref($self), " send failed: $!";
	}

	unless ($dgram) {
		print STDERR ">>> $out";
	}
}

sub read_datagram {
	my $self = shift;
	my $dgram = shift;

	my $from = recv(STDIN, my $in, 70000, 0)
	    or die ref($self), " recv from failed: $!";
	# Raw sockets include the IPv4 header.
	if ($self->{socktype} && $self->{socktype} == Socket::SOCK_RAW &&
	    $self->{af} eq "inet") {
		substr($in, 0, 20, "");
	}

	my ($port, $netaddr, $addr);
	if ($self->{af} eq "inet") {
		($port, $netaddr) = unpack_sockaddr_in($from);
		$addr = inet_ntop(AF_INET, $netaddr);
	} else {
		($port, $netaddr) = unpack_sockaddr_in6($from);
		$addr = inet_ntop(AF_INET6, $netaddr);
	}
	$self->{fromaddr} = $addr;
	$self->{fromport} = $port;
	print STDERR "recv from: $addr $port\n";

	if ($dgram) {
		$$dgram = $in;
	} else {
		print STDERR "<<< $in";
	}
}

sub write_read_datagram {
	my $self = shift;
	write_datagram($self);
	read_datagram($self);
}

sub read_write_datagram {
	my $self = shift;
	read_datagram($self);
	$self->{toaddr} = $self->{fromaddr};
	$self->{toport} = $self->{fromport};
	write_datagram($self);
}

sub read_write_packet {
	my $self = shift;

	my $packet;
	read_datagram($self, \$packet);
	my $hexin = unpack("H*", $packet);
	print STDERR "<<< $hexin\n";

	$packet =~ s/Client|Server/Packet/;
	$self->{toaddr} = $self->{fromaddr};
	$self->{toport} = $self->{fromport};
	write_datagram($self, $packet);
	my $hexout = unpack("H*", $packet);
	print STDERR ">>> $hexout\n";
}

sub in_cksum {
	my $data = shift;
	my $sum = 0;

	$data .= pack("x") if (length($data) & 1);
	while (length($data)) {
		$sum += unpack("n", substr($data, 0, 2, ""));
		$sum = ($sum >> 16) + ($sum & 0xffff) if ($sum > 0xffff);
	}
	return (~$sum & 0xffff);
}

use constant IPPROTO_ICMPV6	=> 58;
use constant ICMP_ECHO		=> 8;
use constant ICMP_ECHOREPLY	=> 0;
use constant ICMP6_ECHO_REQUEST	=> 128;
use constant ICMP6_ECHO_REPLY	=> 129;

my $seq = 0;
sub write_icmp_echo {
	my $self = shift;
	my $pid = shift || $$;
	my $af = $self->{af};

	my $type = $af eq "inet" ? ICMP_ECHO : ICMP6_ECHO_REQUEST;
	# type, code, cksum, id, seq
	my $icmp = pack("CCnnn", $type, 0, 0, $pid, ++$seq);
	if ($af eq "inet") {
		substr($icmp, 2, 2, pack("n", in_cksum($icmp)));
	} else {
		# src, dst, plen, pad, next
		my $phdr = "";
		$phdr .= inet_pton(AF_INET6, $self->{srcaddr});
		$phdr .= inet_pton(AF_INET6, $self->{dstaddr});
		$phdr .= pack("NxxxC", length($icmp), IPPROTO_ICMPV6);
		print STDERR "pseudo header: ", unpack("H*", $phdr), "\n";
		substr($icmp, 2, 2, pack("n", in_cksum($phdr. $icmp)));
	}

	write_datagram($self, $icmp);
	my $text = $af eq "inet" ? "ICMP" : "ICMP6";
	print STDERR ">>> $text ", unpack("H*", $icmp), "\n";
}

sub read_icmp_echo {
	my $self = shift;
	my $reply = shift;
	my $af = $self->{af};

	my $icmp;
	read_datagram($self, \$icmp);

	my $text = $af eq "inet" ? "ICMP" : "ICMP6";
	$text .= " reply" if $reply;
	my $phdr = "";
	if ($af eq "inet6") {
		# src, dst, plen, pad, next
		$phdr .= inet_pton(AF_INET6, $self->{srcaddr});
		$phdr .= inet_pton(AF_INET6, $self->{dstaddr});
		$phdr .= pack("NxxxC", length($icmp), IPPROTO_ICMPV6);
		print STDERR "pseudo header: ", unpack("H*", $phdr), "\n";
	}
	if (length($icmp) < 8) {
		$text = "BAD $text LENGTH";
	} elsif (in_cksum($phdr. $icmp) != 0) {
		$text = "BAD $text CHECKSUM";
	} else {
		my($type, $code, $cksum, $id, $seq) = unpack("CCnnn", $icmp);
		my $t = $reply ?
		    ($af eq "inet" ? ICMP_ECHOREPLY : ICMP6_ECHO_REPLY) :
		    ($af eq "inet" ? ICMP_ECHO : ICMP6_ECHO_REQUEST);
		if ($type != $t) {
			$text = "BAD $text TYPE";
		} elsif ($code != 0) {
			$text = "BAD $text CODE";
		}
	}

	print STDERR "<<< $text ", unpack("H*", $icmp), "\n";
}

########################################################################
# Script funcs
########################################################################

sub check_logs {
	my ($c, $r, $s, %args) = @_;

	return if $args{nocheck};

	check_inout($c, $r, $s, %args);
}

sub check_inout {
	my ($c, $r, $s, %args) = @_;

	if ($args{client} && !$args{client}{nocheck}) {
		my $out = $args{client}{out} || "Client";
		$c->loggrep(qr/^>>> $out/) or die "no client output"
		    unless $args{client}{noout};
		my $in = $args{client}{in} || "Server";
		$c->loggrep(qr/^<<< $in/) or die "no client input"
		    unless $args{client}{noin};
	}
	if ($args{packet} && !$args{packet}{nocheck}) {
		my $hex;
		my $in = $args{packet}{in} || $args{packet}{noin}
		    or die "no packet input regex";
		$hex = unpack("H*", $in);
		$r->loggrep(qr/Packet: <<< .*$hex/) or die "no packet input"
		    unless $args{packet}{noin};
		my $out = $args{packet}{out} || "Packet";
		$hex = unpack("H*", $out);
		$r->loggrep(qr/Packet: >>> .*$hex/) or die "no packet output"
		    unless $args{packet}{noout};
	}
	if ($args{server} && !$args{server}{nocheck}) {
		my $in = $args{server}{in} || "Client";
		$s->loggrep(qr/^<<< $in/) or die "no server input"
		    unless $args{server}{noin};
		my $out = $args{server}{out} || "Server";
		$s->loggrep(qr/^>>> $out/) or die "no server output"
		    unless $args{server}{noout};
	}
}

1;
