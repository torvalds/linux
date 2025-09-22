#	$OpenBSD: Proc.pm,v 1.6 2017/12/18 17:01:27 bluhm Exp $

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

package Proc;
use Carp;
use IO::File;
use List::Util qw(first);
use POSIX;
use Time::HiRes qw(time alarm sleep);

my %CHILDREN;

BEGIN {
	$SIG{TERM} = $SIG{INT} = sub {
		my $sig = shift;
		kill TERM => keys %CHILDREN;
		$SIG{TERM} = $SIG{INT} = 'DEFAULT';
		POSIX::raise($sig);
	};
}

END {
	kill TERM => keys %CHILDREN;
	$SIG{TERM} = $SIG{INT} = 'DEFAULT';
}

sub new {
	my $class = shift;
	my $self = { @_ };
	$self->{down} ||= $self->{alarm} ? "Alarm $class" : "Shutdown $class";
	$self->{func} && ref($self->{func}) eq 'CODE'
	    or croak "$class func not given";
	!$self->{ktrace} || $self->{ktracefile}
	    or croak "$class ktrace file not given";
	$self->{logfile}
	    or croak "$class log file not given";
	open(my $fh, '>', $self->{logfile})
	    or die "$class log file $self->{logfile} create failed: $!";
	$fh->autoflush;
	$self->{log} = $fh;
	return bless $self, $class;
}

sub run {
	my $self = shift;

	defined(my $pid = fork())
	    or die ref($self), " fork child failed: $!";
	if ($pid) {
		$CHILDREN{$pid} = 1;
		$self->{pid} = $pid;
		return $self;
	}
	%CHILDREN = ();
	$SIG{TERM} = $SIG{INT} = 'DEFAULT';
	$SIG{__DIE__} = sub {
		die @_ if $^S;
		warn @_;
		IO::Handle::flush(\*STDERR);
		POSIX::_exit(255);
	};
	open(STDERR, '>&', $self->{log})
	    or die ref($self), " dup STDERR failed: $!";

	if ($self->{ktrace}) {
		my @cmd = ("ktrace", "-af", $self->{ktracefile}, "-p", $$);
		do { local $> = 0; system(@cmd) }
		    and die ref($self), " system '@cmd' failed: $?";
		my $uid = $>;
		do { local $> = 0; chown $uid, -1, $self->{ktracefile} }
		    or die ref($self),
		    " chown $uid '$self->{ktracefile}' failed: $?";
	}

	$self->child();
	print STDERR $self->{up}, "\n";
	alarm($self->{alarm}) if $self->{alarm};
	$self->{func}->($self);
	print STDERR "Shutdown ", ref($self), "\n";

	IO::Handle::flush(\*STDOUT);
	IO::Handle::flush(\*STDERR);
	POSIX::_exit(0);
}

sub wait {
	my $self = shift;
	my $flags = shift;

	my $pid = $self->{pid}
	    or croak ref($self), " no child pid";
	my $kid = waitpid($pid, $flags);
	if ($kid > 0) {
		my $status = $?;
		my $code;
		$code = "exit: ".   WEXITSTATUS($?) if WIFEXITED($?);
		$code = "signal: ". WTERMSIG($?)    if WIFSIGNALED($?);
		$code = "stop: ".   WSTOPSIG($?)    if WIFSTOPPED($?);
		return wantarray ? ($kid, $status, $code) : $kid;
	}
	return $kid;
}

sub loggrep {
	my $self = shift;
	my($regex, $timeout) = @_;

	my $end;
	$end = time() + $timeout if $timeout;

	do {
		my($kid, $status, $code) = $self->wait(WNOHANG);
		if ($self->{alarm} && $kid > 0 &&
		    WIFSIGNALED($status) && WTERMSIG($status) == 14 ) {
			# child killed by SIGALRM as expected
			print {$self->{log}} "Alarm ", ref($self), "\n";
		} elsif ($kid > 0 && $status != 0) {
			# child terminated with failure
			die ref($self), " child status: $status $code";
		}
		open(my $fh, '<', $self->{logfile})
		    or die ref($self), " log file open failed: $!";
		my $match = first { /$regex/ } <$fh>;
		return $match if $match;
		close($fh);
		# pattern not found
		if ($kid == 0) {
			# child still running, wait for log data
			sleep .1;
		} else {
			# child terminated, no new log data possible
			return;
		}
	} while ($timeout and time() < $end);

	return;
}

sub up {
	my $self = shift;
	my $timeout = shift || 10;
	$self->loggrep(qr/$self->{up}/, $timeout)
	    or croak ref($self), " no '$self->{up}' in $self->{logfile} ".
		"after $timeout seconds";
	return $self;
}

sub down {
	my $self = shift;
	my $timeout = shift || 20;
	$self->loggrep(qr/$self->{down}/, $timeout)
	    or croak ref($self), " no '$self->{down}' in $self->{logfile} ".
		"after $timeout seconds";
	return $self;
}

1;
