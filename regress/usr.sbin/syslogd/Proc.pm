#	$OpenBSD: Proc.pm,v 1.10 2022/03/25 14:15:10 bluhm Exp $

# Copyright (c) 2010-2020 Alexander Bluhm <bluhm@openbsd.org>
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
use BSD::Resource qw(getrlimit setrlimit get_rlimits);
use Carp;
use Errno;
use IO::File;
use POSIX;
use Time::HiRes qw(time alarm sleep);
use IO::Socket::SSL;

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
	$self->{ktracepid} && $self->{ktraceexec}
	    and croak "$class ktrace both pid and exec given";
	!($self->{ktracepid} || $self->{ktraceexec}) || $self->{ktracefile}
	    or croak "$class ktrace file not given";
	$self->{logfile}
	    or croak "$class log file not given";
	open(my $fh, '>', $self->{logfile})
	    or die "$class log file $self->{logfile} create failed: $!";
	$fh->autoflush;
	$self->{log} = $fh;
	$self->{ppid} = $$;
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

	if ($self->{rlimit}) {
		my $rlimits = get_rlimits()
		    or die ref($self), " get_rlimits failed: $!";
		while (my($name, $newsoft) = each %{$self->{rlimit}}) {
			defined(my $resource = $rlimits->{$name})
			    or die ref($self), " rlimit $name does not exists";
			my ($soft, $hard) = getrlimit($resource)
			    or die ref($self), " getrlimit $name failed: $!";
			setrlimit($resource, $newsoft, $hard) or die ref($self),
			    " setrlimit $name to $newsoft failed: $!";
		}
	}
	if ($self->{ktracepid}) {
		my @cmd = ($self->{ktracepid}, "-i", "-f", $self->{ktracefile},
		    "-p", $$);
		system(@cmd)
		    and die ref($self), " system '@cmd' failed: $?";
	}
	do {
		$self->child();
		print STDERR $self->{up}, "\n";
		$self->{ts} = $self->{cs}
		    if $self->{connectproto} && $self->{connectproto} eq "tls";
		$self->{func}->($self);
		$self->{ts}->close(SSL_fast_shutdown => 0)
		    or die ref($self), " SSL shutdown: $!,$SSL_ERROR"
		    if $self->{ts};
		delete $self->{ts};
	} while ($self->{redo});
	print STDERR "Shutdown", "\n";

	IO::Handle::flush(\*STDOUT);
	IO::Handle::flush(\*STDERR);
	POSIX::_exit(0);
}

sub wait {
	my $self = shift;
	my $flags = shift;

	# if we a not the parent process, assume the child is still running
	return 0 unless $self->{ppid} == $$;

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
	my($regex, $timeout, $count) = @_;
	my $exit = ($self->{exit} // 0) << 8;

	my $end;
	$end = time() + $timeout if $timeout;

	do {
		my($kid, $status, $code) = $self->wait(WNOHANG);
		if ($kid > 0 && $status != $exit) {
			# child terminated with failure
			die ref($self), " child status: $status $code";
		}
		open(my $fh, '<', $self->{logfile})
		    or die ref($self), " log file open failed: $!";
		my @match = grep { /$regex/ } <$fh>;
		return wantarray ? @match : $match[0]
		    if !$count && @match or $count && @match >= $count;
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
	my $timeout = shift || 60;
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

sub kill {
	my $self = shift;
	my $sig = shift // 'TERM';
	my $pid = shift // $self->{pid};

	if (kill($sig => $pid) != 1) {
		my $sudo = $ENV{SUDO};
		$sudo && $!{EPERM}
		    or die ref($self), " kill $pid failed: $!";
		my @cmd = ($sudo, '/bin/kill', "-$sig", $pid);
		system(@cmd)
		    and die ref($self), " sudo kill $pid failed: $?";
	}
	return $self;
}

1;
