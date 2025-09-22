#	$OpenBSD: Packet.pm,v 1.5 2021/12/12 21:16:53 bluhm Exp $

# Copyright (c) 2010-2017 Alexander Bluhm <bluhm@openbsd.org>
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
use parent 'Proc';
use Carp;
use Socket;
use Socket6;
use IO::Socket;
use IO::Socket::IP -register;

use constant IPPROTO_DIVERT => 258;

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktracefile} ||= "packet.ktrace";
	$args{logfile} ||= "packet.log";
	$args{up} ||= "Bound";
	$args{down} ||= "Shutdown $class";
	my $self = Proc::new($class, %args);
	$self->{domain}
	    or croak "$class domain not given";

	if ($self->{ktrace}) {
		unlink $self->{ktracefile};
		my @cmd = ("ktrace", "-f", $self->{ktracefile}, "-p", $$);
		do { local $> = 0; system(@cmd) }
		    and die ref($self), " system '@cmd' failed: $?";
	}

	my $ds = do { local $> = 0; IO::Socket->new(
	    Type	=> Socket::SOCK_RAW,
	    Proto	=> IPPROTO_DIVERT,
	    Domain	=> $self->{domain},
	) } or die ref($self), " socket failed: $!";
	my $sa;
	$sa = pack_sockaddr_in($self->{bindport}, Socket::INADDR_ANY)
	    if $self->{af} eq "inet";
	$sa = pack_sockaddr_in6($self->{bindport}, Socket::IN6ADDR_ANY)
	    if $self->{af} eq "inet6";
	$ds->bind($sa)
	    or die ref($self), " bind failed: $!";
	my $log = $self->{log};
	print $log "divert sock: ",$ds->sockhost()," ",$ds->sockport(),"\n";
	$self->{divertaddr} = $ds->sockhost();
	$self->{divertport} = $ds->sockport();
	$self->{ds} = $ds;

	if ($self->{ktrace}) {
		my @cmd = ("ktrace", "-c", "-f", $self->{ktracefile}, "-p", $$);
		do { local $> = 0; system(@cmd) }
		    and die ref($self), " system '@cmd' failed: $?";
	}

	return $self;
}

sub child {
	my $self = shift;
	my $ds = $self->{ds};

	open(STDIN, '<&', $ds)
	    or die ref($self), " dup STDIN failed: $!";
	open(STDOUT, '>&', $ds)
	    or die ref($self), " dup STDOUT failed: $!";
}

1;
