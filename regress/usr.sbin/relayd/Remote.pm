#	$OpenBSD: Remote.pm,v 1.7 2016/05/03 19:13:04 bluhm Exp $

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

package Remote;
use parent 'Proc';
use Carp;
use Cwd;
use File::Basename;

my %PIPES;

sub close_pipes {
	my @pipes = @_ ? @_ : keys %PIPES
	    or return;
	foreach (@pipes) {
		# file descriptor cannot be a hash key, so use hash value
		my $fh = $PIPES{$_};
		# also print new line as close is delayed by forked processes
		print $fh "close\n";
		close($fh);
	}
	sleep 1;  # give other end a chance to finish process
	delete @PIPES{@pipes};
}

END {
	close_pipes();
}

sub new {
	my $class = shift;
	my %args = @_;
	$args{logfile} ||= "remote.log";
	$args{up} ||= "listen sock: ";
	$args{down} ||= $args{dryrun} ? "relayd.conf" : "parent terminating";
	$args{func} = sub { Carp::confess "$class func may not be called" };
	$args{remotessh}
	    or croak "$class remote ssh host not given";
	$args{forward}
	    or croak "$class forward not given";
	my $self = Proc::new($class, %args);
	$self->{listenaddr}
	    or croak "$class listen addr not given";
	$self->{connectaddr}
	    or croak "$class connect addr not given";
	$self->{connectport}
	    or croak "$class connect port not given";
	return $self;
}

sub run {
	my $self = Proc::run(shift, @_);
	$PIPES{$self->{pipe}} = $self->{pipe};
	return $self;
}

sub up {
	my $self = Proc::up(shift, @_);
	my $lsock = $self->loggrep(qr/^listen sock: /)
	    or croak ref($self), " no 'listen sock: ' in $self->{logfile}";
	my($addr, $port) = $lsock =~ /: (\S+) (\S+)$/
	    or croak ref($self), " no listen addr and port in $self->{logfile}";
	$self->{listenaddr} = $addr;
	$self->{listenport} = $port;
	return $self;
}

sub child {
	my $self = shift;

	my @opts = $ENV{SSH_OPTIONS} ? split(' ', $ENV{SSH_OPTIONS}) : ();
	my @sudo = $ENV{SUDO} ? "SUDO=$ENV{SUDO}" : ();
	my @ktrace = $ENV{KTRACE} ? "KTRACE=$ENV{KTRACE}" : ();
	my @relayd = $ENV{RELAYD} ? "RELAYD=$ENV{RELAYD}" : ();
	my $dir = dirname($0);
	$dir = getcwd() if ! $dir || $dir eq ".";
	my @cmd = ("ssh", @opts, $self->{remotessh},
	    @sudo, @ktrace, @relayd, "perl",
	    "-I", $dir, "$dir/".basename($0), $self->{forward},
	    $self->{listenaddr}, $self->{connectaddr}, $self->{connectport},
	    ($self->{testfile} ? "$dir/".basename($self->{testfile}) : ()));
	print STDERR "execute: @cmd\n";
	exec @cmd;
	die ref($self), " exec '@cmd' failed: $!";
}

sub close_child {
	my $self = shift;
	close_pipes(delete $self->{pipe});
	return $self;
}

1;
