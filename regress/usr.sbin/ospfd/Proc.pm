#	$OpenBSD: Proc.pm,v 1.4 2016/05/03 19:13:04 bluhm Exp $

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

package Proc;
use Carp;
use Errno;
use IO::File;
use POSIX;
use Time::HiRes qw(time alarm sleep);

my %CHILDREN;

sub kill_children {
	my @pids = @_ ? @_ : keys %CHILDREN
	    or return;
	my @perms;
	foreach my $pid (@pids) {
		if (kill(TERM => $pid) != 1 and $!{EPERM}) {
			push @perms, $pid;
		}
	}
	if (my $sudo = $ENV{SUDO} and @perms) {
		local $?;  # do not modify during END block
		my @cmd = ($sudo, '/bin/kill', '-TERM', @perms);
		system(@cmd);
	}
	delete @CHILDREN{@pids};
}

BEGIN {
	$SIG{TERM} = $SIG{INT} = sub {
		my $sig = shift;
		kill_children();
		$SIG{TERM} = $SIG{INT} = 'DEFAULT';
		POSIX::raise($sig);
	};
}

END {
	kill_children();
	$SIG{TERM} = $SIG{INT} = 'DEFAULT';
}

sub new {
	my $class = shift;
	my $self = { @_ };
	$self->{down} ||= "Shutdown";
	$self->{func} && ref($self->{func}) eq 'CODE'
	    or croak "$class func not given";
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

	pipe(my $reader, my $writer)
	    or die ref($self), " pipe to child failed: $!";
	defined(my $pid = fork())
	    or die ref($self), " fork child failed: $!";
	if ($pid) {
		$CHILDREN{$pid} = 1;
		$self->{pid} = $pid;
		close($reader);
		$self->{pipe} = $writer;
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
	open(STDOUT, '>&', $self->{log})
	    or die ref($self), " dup STDOUT failed: $!";
	close($writer);
	open(STDIN, '<&', $reader)
	    or die ref($self), " dup STDIN failed: $!";
	close($reader);

	$self->child();
	print STDERR $self->{up}, "\n";
	$self->{func}->($self);
	print STDERR "Shutdown", "\n";

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
		delete $CHILDREN{$pid} if WIFEXITED($?) || WIFSIGNALED($?);
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
		if ($kid > 0 && $status != 0) {
			# child terminated with failure
			die ref($self), " child status: $status $code";
		}
		open(my $fh, '<', $self->{logfile})
		    or die ref($self), " log file open failed: $!";
		my @match = grep { /$regex/ } <$fh>;
		return wantarray ? @match : $match[0] if @match;
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
	my $timeout = shift || 30;
	$self->loggrep(qr/$self->{down}/, $timeout)
	    or croak ref($self), " no '$self->{down}' in $self->{logfile} ".
		"after $timeout seconds";
	return $self;
}

sub kill_child {
	my $self = shift;
	kill_children($self->{pid});
	return $self;
}

1;
