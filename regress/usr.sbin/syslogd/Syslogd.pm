#	$OpenBSD: Syslogd.pm,v 1.26 2021/03/09 15:16:28 bluhm Exp $

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

package Syslogd;
use parent 'Proc';
use Carp;
use Cwd;
use File::Basename;
use File::Copy;
use File::Temp qw(tempfile tempdir);
use Sys::Hostname;
use Time::HiRes qw(time alarm sleep);

sub new {
	my $class = shift;
	my %args = @_;
	$args{ktraceexec} = "ktrace" if $args{ktrace};
	$args{ktraceexec} = $ENV{KTRACE} if $ENV{KTRACE};
	$args{ktracefile} ||= "syslogd.ktrace";
	$args{fstatfile} ||= "syslogd.fstat";
	$args{logfile} ||= "syslogd.log";
	$args{up} ||= "syslogd: started";
	$args{down} ||= "syslogd: exited";
	$args{up} = $args{down} = "execute:"
	    if $args{foreground} || $args{daemon};
	$args{foreground} && $args{daemon}
	    and croak "$class cannot run in foreground and as daemon";
	$args{func} = sub { Carp::confess "$class func may not be called" };
	$args{execfile} ||= $ENV{SYSLOGD} ? $ENV{SYSLOGD} : "syslogd";
	$args{conffile} ||= "syslogd.conf";
	$args{outfile} ||= "file.log";
	unless ($args{outpipe}) {
		my $dir = tempdir("syslogd-regress-XXXXXXXXXX",
		    CLEANUP => 1, TMPDIR => 1);
		chmod(0755, $dir)
		    or die "$class chmod directory $dir failed: $!";
		$args{tempdir} = $dir;
		$args{outpipe} = "$dir/pipe.log";
	}
	$args{outconsole} ||= "console.log";
	$args{outuser} ||= "user.log";
	if ($args{memory}) {
		$args{memory} = {} unless ref $args{memory};
		$args{memory}{name} ||= "memory";
		$args{memory}{size} //= 1;
	}
	my $self = Proc::new($class, %args);
	$self->{connectaddr}
	    or croak "$class connect addr not given";

	_make_abspath(\$self->{$_}) foreach (qw(conffile outfile outpipe));
	_make_abspath(\$self->{ktracefile}) if $self->{chdir};

	# substitute variables in config file
	my $curdir = dirname($0) || ".";
	my $objdir = getcwd();
	my $hostname = hostname();
	(my $host = $hostname) =~ s/\..*//;
	my $connectdomain = $self->{connectdomain};
	my $connectaddr = $self->{connectaddr};
	my $connectproto = $self->{connectproto};
	my $connectport = $self->{connectport};

	open(my $fh, '>', $self->{conffile})
	    or die ref($self), " create conf file $self->{conffile} failed: $!";
	print $fh "*.*\t$self->{outfile}\n";
	print $fh "*.*\t|dd of=$self->{outpipe}\n" unless $self->{nopipe};
	print $fh "*.*\t/dev/console\n" unless $self->{noconsole};
	print $fh "*.*\tsyslogd-regress\n" unless $self->{nouser};
	my $memory = $self->{memory};
	print $fh "*.*\t:$memory->{size}:$memory->{name}\n" if $memory;
	my $loghost = $self->{loghost};
	unless ($loghost) {
		$loghost = '@$connectaddr';
		$loghost .= ':$connectport' if $connectport;
	}
	my $config = "*.*\t$loghost\n";
	$config .= $self->{conf} if $self->{conf};
	$config =~ s/(\$[a-z]+)/$1/eeg;
	print $fh $config;
	close $fh;

	return $self->create_out();
}

sub create_out {
	my $self = shift;
	my $timeout = shift || 10;
	my @sudo = $ENV{SUDO} ? $ENV{SUDO} : ();

	my $end = time() + $timeout;

	open(my $fh, '>', $self->{outfile})
	    or die ref($self), " create log file $self->{outfile} failed: $!";
	close $fh;

	open($fh, '>', $self->{outpipe})
	    or die ref($self), " create pipe file $self->{outpipe} failed: $!";
	chmod(0644, $self->{outpipe})
	    or die ref($self), " chmod pipe file $self->{outpipe} failed: $!";
	my @cmd = (@sudo, "chown", "_syslogd", $self->{outpipe});
	system(@cmd)
	    and die ref($self), " chown pipe file $self->{outpipe} failed: $!";
	close $fh;

	foreach my $dev (qw(console user)) {
		my $file = $self->{"out$dev"};
		unlink($file);
		open($fh, '>', $file)
		    or die ref($self), " create $dev file $file failed: $!";
		close $fh;
		my $user = $dev eq "console" ?
		    "/dev/console" : "syslogd-regress";
		my @cmd = (@sudo, "./ttylog", $user, $file);
		$self->{"pid$dev"} = open(my $ctl, '|-', @cmd)
		    or die ref($self), " pipe to @cmd failed: $!";
		# remember until object is destroyed, autoclose will send EOF
		$self->{"ctl$dev"} = $ctl;
	}

	foreach my $dev (qw(console user)) {
		my $file = $self->{"out$dev"};
		while ($self->{"ctl$dev"}) {
			open(my $fh, '<', $file) or die ref($self),
			    " open $file for reading failed: $!";
			last if grep { /ttylog: started/ } <$fh>;
			time() < $end
			    or croak ref($self), " no 'started' in $file ".
			    "after $timeout seconds";
			sleep .1;
		}
	}

	return $self;
}

sub ttykill {
	my $self = shift;
	my $dev = shift;
	my $sig = shift;
	my $pid = $self->{"pid$dev"}
	    or die ref($self), " no tty log pid$dev";

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

sub child {
	my $self = shift;
	my @sudo = $ENV{SUDO} ? $ENV{SUDO} : "env";

	my @pkill = (@sudo, "pkill", "-KILL", "-x", "syslogd");
	my @pgrep = ("pgrep", "-x", "syslogd");
	system(@pkill) && $? != 256
	    and die ref($self), " system '@pkill' failed: $?";
	while ($? == 0) {
		print STDERR "syslogd still running\n";
		system(@pgrep) && $? != 256
		    and die ref($self), " system '@pgrep' failed: $?";
	}
	print STDERR "syslogd not running\n";

	unless (${$self->{client}}->{early}) {
		my @flush = (@sudo, "./logflush");
		system(@flush)
		    and die "Command '@flush' failed: $?";
	}

	chdir $self->{chdir}
	    or die ref($self), " chdir '$self->{chdir}' failed: $!"
	    if $self->{chdir};

	my @libevent;
	foreach (qw(EVENT_NOKQUEUE EVENT_NOPOLL EVENT_NOSELECT)) {
		push @libevent, "$_=1" if delete $ENV{$_};
	}
	push @libevent, "EVENT_SHOW_METHOD=1" if @libevent;
	my @ktrace;
	@ktrace = ($self->{ktraceexec}, "-i", "-f", $self->{ktracefile})
	    if $self->{ktraceexec};
	my @cmd = (@sudo, @libevent, @ktrace, $self->{execfile},
	    "-f", $self->{conffile});
	push @cmd, "-d" if !$self->{foreground} && !$self->{daemon};
	push @cmd, "-F" if $self->{foreground};
	push @cmd, "-V" unless $self->{cacrt};
	push @cmd, "-C", $self->{cacrt}
	    if $self->{cacrt} && $self->{cacrt} ne "default";
	push @cmd, "-s", $self->{ctlsock} if $self->{ctlsock};
	push @cmd, @{$self->{options}} if $self->{options};
	print STDERR "execute: @cmd\n";
	exec @cmd;
	die ref($self), " exec '@cmd' failed: $!";
}

sub up {
	my $self = Proc::up(shift, @_);
	my $timeout = shift || 10;

	my $end = time() + $timeout;

	while ($self->{fstat}) {
		$self->fstat();
		last unless $self->{foreground} || $self->{daemon};

		# in foreground mode and as daemon we have no debug output
		# check fstat kqueue entry to detect statup
		open(my $fh, '<', $self->{fstatfile}) or die ref($self),
		    " open $self->{fstatfile} for reading failed: $!";
		last if grep { /kqueue .* state: W/ } <$fh>;
		time() < $end
		    or croak ref($self), " no 'kqueue' in $self->{fstatfile} ".
		    "after $timeout seconds";
		sleep .1;
	}

	return $self;
}

sub down {
	my $self = shift;

	if (my $dir = $self->{tempdir}) {
		# keep all logs in single directory for easy debugging
		copy($_, ".") foreach glob("$dir/*");
	}

	return Proc::down($self, @_) unless $self->{daemon};

	my $timeout = $_[0] || 10;
	my $end = time() + $timeout;

	my @sudo = $ENV{SUDO} ? $ENV{SUDO} : "env";
	my @pkill = (@sudo, "pkill", "-TERM", "-x", "syslogd");
	my @pgrep = ("pgrep", "-x", "syslogd");
	system(@pkill) && $? != 256
	    and die ref($self), " system '@pkill' failed: $?";
	do {
		sleep .1;
		system(@pgrep) && $? != 256
		    and die ref($self), " system '@pgrep' failed: $?";
		return Proc::down($self, @_) if $? == 256;
		print STDERR "syslogd still running\n";
	} while (time() < $end);

	return;
}

sub fstat {
	my $self = shift;

	open(my $fh, '>', $self->{fstatfile}) or die ref($self),
	    " open $self->{fstatfile} for writing failed: $!";
	my @cmd = ("fstat");
	open(my $fs, '-|', @cmd)
	    or die ref($self), " open pipe from '@cmd' failed: $!";
	print $fh grep { /^\w+ *syslogd *\d+/ } <$fs>;
	close($fs) or die ref($self), $! ?
	    " close pipe from '@cmd' failed: $!" :
	    " command '@cmd' failed: $?";
	close($fh)
	    or die ref($self), " close $self->{fstatfile} failed: $!";
}

sub _make_abspath {
	my $file = ref($_[0]) ? ${$_[0]} : $_[0];
	if (substr($file, 0, 1) ne "/") {
		$file = getcwd(). "/". $file;
		${$_[0]} = $file if ref($_[0]);
	}
	return $file;
}

sub kill_privsep {
	return Proc::kill(@_);
}

sub kill_syslogd {
	my $self = shift;
	my $sig = shift // 'TERM';
	my $ppid = shift // $self->{pid};

	# find syslogd child of privsep parent
	my @cmd = ("ps", "-ww", "-p", $ppid, "-U", "_syslogd",
	    "-o", "pid,ppid,comm", );
	open(my $ps, '-|', @cmd)
	    or die ref($self), " open pipe from '@cmd' failed: $!";
	my @pslist;
	my @pshead = split(' ', scalar <$ps>);
	while (<$ps>) {
		s/\s+$//;
		my %h;
		@h{@pshead} = split(' ', $_, scalar @pshead);
		push @pslist, \%h;
	}
	close($ps) or die ref($self), $! ?
	    " close pipe from '@cmd' failed: $!" :
	    " command '@cmd' failed: $?";
	my @pschild =
	    grep { $_->{PPID} == $ppid && $_->{COMMAND} eq "syslogd" } @pslist;
	@pschild == 1
	    or die ref($self), " not one privsep child: ",
	    join(" ", map { $_->{PID} } @pschild);

	return Proc::kill($self, $sig, $pschild[0]{PID});
}

my $rotate_num = 0;
sub rotate {
	my $self = shift;

	$self->loggrep("bytes transferred", 1) or sleep 1;
	foreach my $name (qw(file pipe)) {
		my $file = $self->{"out$name"};
		for (my $i = $rotate_num; $i >= 0; $i--) {
			my $new = $file. ".$i";
			my $old = $file. ($i > 0 ? ".".($i-1) : "");

			rename($old, $new) or die ref($self),
			    " rename from '$old' to '$new' failed: $!";
		}
	}
	$rotate_num++;
	return $self->create_out();
};

1;
