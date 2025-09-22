#	$OpenBSD: Syslogc.pm,v 1.1 2014/09/13 23:38:24 bluhm Exp $

# Copyright (c) 2010-2014 Alexander Bluhm <bluhm@openbsd.org>
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

package Syslogc;
use parent 'Proc';
use Carp;

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktracefile} ||= "syslogc.ktrace";
	$args{logfile} ||= "syslogc.log";
	$args{ctlsock} ||= "ctl.sock";
	$args{up} ||= "execute: ";
	$args{func} = sub { Carp::confess "$class func may not be called" };
	my $self = Proc::new($class, %args);

	return $self;
}

sub child {
	my $self = shift;
	my @sudo = $ENV{SUDO} ? $ENV{SUDO} : ();

	my @ktrace = $ENV{KTRACE} || ();
	@ktrace = "ktrace" if $self->{ktrace} && !@ktrace;
	push @ktrace, "-i", "-f", $self->{ktracefile} if @ktrace;
	my @cmd = (@sudo, @ktrace, "syslogc", "-s", $self->{ctlsock});
	push @cmd, @{$self->{options}} if $self->{options};
	print STDERR "execute: @cmd\n";
	exec @cmd;
	die ref($self), " exec '@cmd' failed: $!";
}

sub down {
	my $self = shift;
	eval { Proc::down($self, @_) };
	die $@ if $@ && $self->{down} ne "Shutdown";
	return $self;
}

1;
