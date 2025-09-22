# ex:ts=8 sw=4:
# $OpenBSD: BaseState.pm,v 1.5 2025/06/01 00:45:39 bentley Exp $
#
# Copyright (c) 2007-2022 Marc Espie <espie@openbsd.org>
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
#

use v5.36;

package OpenBSD::BaseState;
use Carp;

sub can_output($)
{
	1;
}
sub sync_display($)
{
}

my $forbidden = qr{[^[:print:]\s]};

sub safe($self, $string)
{
	$string =~ s/$forbidden/?/g;
	return $string;
}

sub f($self, @p)
{
	if (@p == 0) {
		return undef;
	}
	my ($fmt, @l) = @p;

	# is there anything to format, actually ?
	if ($fmt =~ m/\#\d/) {
		# encode any unknown chars as ?
		for (@l) {
			s/$forbidden/?/g if defined;
		}
		# make it so that #0 is #
		unshift(@l, '#');
		$fmt =~ s,\#(\d+),($l[$1] // "<Undefined #$1>"),ge;
	}
	return $fmt;
}

sub _fatal($self, @p)
{
	# implementation note: to print "fatal errors" elsewhere,
	# the way is to eval { croak @_}; and decide what to do with $@.
	delete $SIG{__DIE__};
	$self->sync_display;
	croak @p, "\n";
}

sub fatal($self, @p)
{
	$self->_fatal($self->f(@p));
}

sub _fhprint($self, $fh, @p)
{
	$self->sync_display;
	print $fh @p;
}
sub _print($self, @p)
{
	$self->_fhprint(\*STDOUT, @p) if $self->can_output;
}

sub _errprint($self, @p)
{
	$self->_fhprint(\*STDERR, @p);
}

sub fhprint($self, $fh, @p)
{
	$self->_fhprint($fh, $self->f(@p));
}

sub fhsay($self, $fh, @p)
{
	if (@p == 0) {
		$self->_fhprint($fh, "\n");
	} else {
		$self->_fhprint($fh, $self->f(@p), "\n");
	}
}

sub print($self, @p)
{
	$self->fhprint(\*STDOUT, @p) if $self->can_output;
}

sub say($self, @p)
{
	$self->fhsay(\*STDOUT, @p) if $self->can_output;
}

sub errprint($self, @p)
{
	$self->fhprint(\*STDERR, @p);
}

sub errsay($self, @p)
{
	$self->fhsay(\*STDERR, @p);
}

my @signal_name = ();
sub fillup_names($)
{
	{
	# XXX force autoload
	package verylocal;

	require POSIX;
	POSIX->import(qw(signal_h));
	}

	for my $sym (keys %POSIX::) {
		next unless $sym =~ /^SIG([A-Z].*)/;
		my $value = eval "&POSIX::$sym()";
		# skip over POSIX stuff we don't have like SIGRT or SIGPOLL
		next unless defined $value;
		$signal_name[$value] = $1;
	}
	# extra BSD signals
	$signal_name[5] = 'TRAP';
	$signal_name[7] = 'IOT';
	$signal_name[10] = 'BUS';
	$signal_name[12] = 'SYS';
	$signal_name[16] = 'URG';
	$signal_name[23] = 'IO';
	$signal_name[24] = 'XCPU';
	$signal_name[25] = 'XFSZ';
	$signal_name[26] = 'VTALRM';
	$signal_name[27] = 'PROF';
	$signal_name[28] = 'WINCH';
	$signal_name[29] = 'INFO';
}

sub find_signal($self, $number)
{
	if (@signal_name == 0) {
		$self->fillup_names;
	}

	return $signal_name[$number] || $number;
}

sub child_error($self, $error = $?)
{
	my $extra = "";

	if ($error & 128) {
		$extra = $self->f(" (core dumped)");
	}
	if ($error & 127) {
		return $self->f("killed by signal #1#2",
		    $self->find_signal($error & 127), $extra);
	} else {
		return $self->f("exit(#1)#2", ($error >> 8), $extra);
	}
}

sub _system($self, @p)
{
	$self->sync_display;
	my ($todo, $todo2);
	if (ref $p[0] eq 'CODE') {
		$todo = shift @p;
	} else {
		$todo = sub() {};
	}
	if (ref $p[0] eq 'CODE') {
		$todo2 = shift @p;
	} else {
		$todo2 = sub() {};
	}
	my $r = fork;
	if (!defined $r) {
		return 1;
	} elsif ($r == 0) {
		$DB::inhibit_exit = 0;
		&$todo();
		exec {$p[0]} @p or
		    exit 1;
	} else {
		&$todo2();
		waitpid($r, 0);
		return $?;
	}
}

sub system($self, @p)
{
	my $r = $self->_system(@p);
	if ($r != 0) {
		if (ref $p[0] eq 'CODE') {
			shift @p;
		}
		if (ref $p[0] eq 'CODE') {
			shift @p;
		}
		$self->errsay("system(#1) failed: #2",
		    join(", ", @p), $self->child_error);
	}
	return $r;
}

sub verbose_system($self, @p)
{
	if (ref $p[0]) {
		shift @p;
	}
	if (ref $p[0]) {
		shift @p;
	}

	$self->print("Running #1", join(' ', @p));
	my $r = $self->_system(@p);
	if ($r != 0) {
		$self->say("... failed: #1", $self->child_error);
	} else {
		$self->say;
	}
}

sub copy_file($self, @p)
{
	require File::Copy;

	my $r = File::Copy::copy(@p);
	if (!$r) {
		$self->say("copy(#1) failed: #2", join(',', @p), $!);
	}
	return $r;
}

sub unlink($self, $verbose, @p)
{
	my $r = unlink @p;
	if ($r != @p) {
		$self->say("rm #1 failed: removed only #2 targets, #3",
		    join(' ', @p), $r, $!);
	} elsif ($verbose) {
		$self->say("rm #1", join(' ', @p));
	}
	return $r;
}

sub copy($self, @p)
{
	require File::Copy;

	my $r = File::Copy::copy(@p);
	if (!$r) {
		$self->say("copy(#1) failed: #2", join(',', @p), $!);
	}
	return $r;
}

sub change_user($self, $uid, $gid)
{
	$( = $gid;
	$) = "$gid $gid";
	$< = $uid;
	$> = $uid;
}

1;
