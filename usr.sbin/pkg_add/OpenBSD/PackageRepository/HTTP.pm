#! /usr/bin/perl
# ex:ts=8 sw=4:
# $OpenBSD: HTTP.pm,v 1.16 2023/07/03 17:01:59 espie Exp $
#
# Copyright (c) 2011 Marc Espie <espie@openbsd.org>
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

use v5.36;

use OpenBSD::PackageRepository::Persistent;

package OpenBSD::PackageRepository::HTTP1;
our @ISA = qw(OpenBSD::PackageRepository::Persistent);
sub urlscheme($)
{
	return 'http';
}

sub initiate($self)
{
	my ($rdfh, $wrfh);
	pipe($self->{getfh}, $wrfh) or die;
	pipe($rdfh, $self->{cmdfh}) or die;

	my $old =select $self->{getfh};
	$| = 1;
	select $self->{cmdfh};
	$| = 1;
	select $rdfh;
	$| = 1;
	select $wrfh;
	$| = 1;
	select $old;
	my $pid = fork();
	if ($pid == 0) {
		close($self->{getfh});
		close($self->{cmdfh});
#		close(STDOUT);
#		close(STDIN);
		open(STDOUT, '>&', $wrfh);
		open(STDIN, '<&', $rdfh);
		_Proxy::main($self);
	} else {
		close($rdfh);
		close($wrfh);
		$self->{controller} = $pid;
	}
}

package _Proxy::Header;

sub new($class)
{
	bless {}, $class;
}

sub code($self)
{
	return $self->{code};
}

package _Proxy::Connection;
sub new($class, $host, $port)
{
	require IO::Socket::INET;
	my $o = IO::Socket::INET->new(
		PeerHost => $host,
		PeerPort => $port);
	my $old = select($o);
	$| = 1;
	select($old);
	bless {fh => $o, host => $host, buffer => ''}, $class;
}

sub send_header($o, $document, %extra)
{
	my $crlf="\015\012";
	$o->print("GET $document HTTP/1.1", $crlf,
	    "Host: ", $o->{host}, $crlf);
	if (defined $extra{range}) {
		my ($a, $b) = @{$extra{range}};
	    	$o->print("Range: bytes=$a-$b", $crlf);
	}
	$o->print($crlf);
}

sub get_header($o)
{
	my $l = $o->getline;
	if ($l !~ m,^HTTP/1\.1\s+(\d\d\d),) {
		return undef;
	}
	my $h = _Proxy::Header->new;
	$h->{code} = $1;
	while ($l = $o->getline) {
		last if $l =~ m/^$/;
		if ($l =~ m/^([\w\-]+)\:\s*(.*)$/) {
			$h->{$1} = $2;
		} else {
			print STDERR "unknown line: $l\n";
		}
	}
	if (defined $h->{'Content-Length'}) {
		$h->{length} = $h->{'Content-Length'}
	} elsif (defined $h->{'Transfer-Encoding'} && 
	    $h->{'Transfer-Encoding'} eq 'chunked') {
		$h->{chunked} = 1;
	}
	if (defined $h->{'Content-Range'} && 
	    $h->{'Content-Range'} =~ m/^bytes\s+(\d+)\-(\d+)\/(\d+)/) {
		($h->{start}, $h->{end}, $h->{size}) = ($1, $2, $3);
	}
	$o->{header} = $h;
	return $h;
}

sub getline($self)
{
	while (1) {
		if ($self->{buffer} =~ s/^(.*?)\015\012//) {
			return $1;
		}
		my $buffer;
		$self->{fh}->recv($buffer, 1024);
		$self->{buffer}.=$buffer;
    	}
}

sub retrieve($self, $sz)
{
	while(length($self->{buffer}) < $sz) {
		my $buffer;
		$self->{fh}->recv($buffer, $sz - length($self->{buffer}));
		$self->{buffer}.=$buffer;
	}
	my $result= substr($self->{buffer}, 0, $sz);
	$self->{buffer} = substr($self->{buffer}, $sz);
	return $result;
}

sub retrieve_and_print($self, $sz, $fh)
{
	my $result = substr($self->{buffer}, 0, $sz);
	print $fh $result;
	my $retrieved = length($result);
	if ($retrieved == $sz) {
		$self->{buffer} = substr($self->{buffer}, $sz);
		return;
	} else {
		$self->{buffer} = '';
	}
	while ($retrieved < $sz) {
		$self->{fh}->recv($result, $sz - $retrieved);
		print $fh $result;
		$retrieved += length($result);
	}
}

sub retrieve_chunked($self)
{
	my $result = '';
	while (1) {
		my $sz = $self->getline;
		if ($sz =~ m/^([0-9a-fA-F]+)/) {
			my $realsize = hex($1);
			last if $realsize == 0;
			$result .= $self->retrieve($realsize);
		}
	}
	return $result;
}

sub retrieve_response($self, $h)
{
	if ($h->{chunked}) {
		return $self->retrieve_chunked;
	}
	if ($h->{length}) {
		return $self->retrieve($h->{length});
	}
	return undef;
}

sub retrieve_response_and_print($self, $h, $fh)
{
	if ($h->{chunked}) {
		print $fh $self->retrieve_chunked;
	}
	if ($h->{length}) {
		$self->retrieve_and_print($h->{length}, $fh);
	}
}

sub print($self, @l)
{
#	print STDERR "Before print\n";
	if (!print {$self->{fh}} @l) {
		print STDERR "network print failed with $!\n";
	}
#	print STDERR "After print\n";
}

package _Proxy;

my $pid;
my $token = 0;

sub batch($code)
{
	if (defined $pid) {
		waitpid($pid, 0);
		undef $pid;
	}
	$token++;
	$pid = fork();
	if (!defined $pid) {
		print "ERROR: fork failed: $!\n";
	}
	if ($pid == 0) {
		&$code();
		exit(0);
	}
}

sub abort_batch()
{
	if (defined $pid) {
		kill HUP => $pid;
		waitpid($pid, 0);
		undef $pid;
	}
	print "\nABORTED $token\n";
}

sub get_directory($o, $dname)
{
	local $SIG{'HUP'} = 'IGNORE';
	$o->send_header("$dname/");
	my $h = $o->get_header;
	if (!defined $h) {
		print "ERROR: can't decode header\n";
		exit 1;
	}

	my $r = $o->retrieve_response($h);
	if (!defined $r) {
		print "ERROR: can't decode response\n";
	}
	if ($h->code != 200) {
			print "ERROR: code was ", $h->code, "\n";
			exit 1;
	}
	print "SUCCESS: directory $dname\n";
	for my $pkg ($r =~ m/\<A[^>]*\s+HREF=\"(.+?)\.tgz\"/gio) {
		$pkg = $1 if $pkg =~ m|^.*/(.*)$|;
		# decode uri-encoding; from URI::Escape
		$pkg =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
		print $pkg, "\n";
	}
	print "\n";
	return;
}

use File::Basename;

sub get_file($o, $fname)
{
	my $bailout = 0;
	$SIG{'HUP'} = sub {
		$bailout++;
	};
	my $first = 1;
	my $start = 0;
	my $end = 2000;
	my $total_size = 0;

	do {
		$end *= 2;
		$o->send_header($fname, range => [$start, $end-1]);
		my $h = $o->get_header;
		if (!defined $h) {
			print "ERROR\n";
			exit 1;
		}
		if (defined $h->{size}) {
			$total_size = $h->{size};
		}
		if ($h->code != 200 && $h->code != 206) {
			print "ERROR: code was ", $h->code, "\n";
			my $r = $o->retrieve_response($h);
			exit 1;
		}
		if ($first) {
			print "TRANSFER: $total_size\n";
			$first = 0;
		}
		$o->retrieve_response_and_print($h, \*STDOUT);
		$start = $end;
		if ($bailout) {
			exit 0;
		}
	} while ($end < $total_size);
}

sub main($self)
{
	my $o = _Proxy::Connection->new($self->{host}, "www");
	while (<STDIN>) {
		chomp;
		if (m/^LIST\s+(.*)$/o) {
			my $dname = $1;
			batch(sub() {get_directory($o, $dname);});
		} elsif (m/^GET\s+(.*)$/o) {
			my $fname = $1;
			batch(sub() { get_file($o, $fname);});
		} elsif (m/^BYE$/o) {
			exit(0);
		} elsif (m/^ABORT$/o) {
			abort_batch();
		} else {
			print "ERROR: Unknown command\n";
		}
	}
}

1;
