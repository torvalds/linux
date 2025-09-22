# ex:ts=8 sw=4:
# $OpenBSD: Error.pm,v 1.43 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2004-2010 Marc Espie <espie@openbsd.org>
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

use v5.36;

# this is a set of common classes related to error handling in pkg land

package OpenBSD::Auto;
sub cache :prototype(*&)($sym, $code)
{
	my $callpkg = caller;
	my $actual = sub($self) {
		return $self->{$sym} //= &$code($self);
	};
	no strict 'refs';
	*{$callpkg."::$sym"} = $actual;
}

package OpenBSD::SigHandler;

# instead of "local" sighandlers, let's do objects that revert
# to their former state afterwards
sub new($class)
{
	# keep previous state
	bless {}, $class;
}


sub DESTROY($self)
{
	while (my ($s, $v) = each %$self) {
		$SIG{$s} = $v;
	}
}

sub set($self, @p)
{
	my $v = pop @p;
	for my $s (@p) {
		$self->{$s} = $SIG{$s};
		$SIG{$s} = $v;
	}
	return $self;
}

sub intercept($self, @p)
{
	my $v = pop @p;
	return $self->set(@p, 
	    sub($sig, @) { 
		&$v($sig); 
		$SIG{$sig} = $self->{$sig}; 
		kill -$sig, $$; 
	    });
}

package OpenBSD::Handler;

# a bunch of other modules create persistent state that must be cleaned up
# on exit (temporary files, network connections to abort properly...)
# END blocks would do that (but see below...) but sig handling bypasses that,
# so we MUST install SIG handlers.

# note that END will be run for *each* process, so beware!
# (temp files are registered per pid, for instance, so they only
# get cleaned when the proper pid is used)
# hash of code to run on ANY exit

# hash of code to run on ANY exit
my $atend = {};
# hash of code to run on fatal signals
my $cleanup = {};

sub cleanup($class, $sig)
{
	# XXX note that order of cleanup is "unpredictable"
	for my $v (values %$cleanup) {
		&$v($sig);
	}
}

END {
	# XXX localize $? so that cleanup doesn't fuck up our exit code
	local $?;
	for my $v (values %$atend) {
		&$v(undef);
	}
}

# register each code block "by name" so that we can re-register each
# block several times
sub register($class, $code)
{
	$cleanup->{$code} = $code;
}

sub atend($class, $code)
{
	$cleanup->{$code} = $code;
	$atend->{$code} = $code;
}

my $handler = sub($sig, @) {
	__PACKAGE__->cleanup($sig);
	# after cleanup, just propagate the signal
	$SIG{$sig} = 'DEFAULT';
	kill $sig, $$;
};

sub reset($)
{
	for my $sig (qw(INT QUIT HUP KILL TERM)) {
		$SIG{$sig} = $handler;
	}
}

__PACKAGE__->reset;

package OpenBSD::Error;
require Exporter;
our @ISA=qw(Exporter);
our @EXPORT=qw(try throw catch rethrow INTetc);


our ($FileName, $Line, $FullMessage);

our @INTetc = (qw(INT QUIT HUP TERM));

use Carp;
sub dienow($error, $handler)
{
	if ($error) {
		if ($error =~ m/^(.*?)(?:\s+at\s+(.*)\s+line\s+(\d+)\.?)?$/o) {
			local $_ = $1;
			$FileName = $2;
			$Line = $3;
			$FullMessage = $error;

			$handler->exec($error, $1, $2, $3);
		} else {
			die "Fatal error: can't parse $error";
		}
	}
}

sub try :prototype(&@)($try, $catch)
{
	eval { &$try() };
	dienow($@, $catch);
}

sub throw(@p)
{
	croak @p;

}

sub rethrow($e)
{
	die $e if $e;
}

sub catch :prototype(&)($code)
{
	bless $code, "OpenBSD::Error::catch";
}

sub rmtree($class, @p)
{
	require File::Path;
	require Cwd;

	# XXX make sure we live somewhere
	Cwd::getcwd() || chdir('/');

	File::Path::rmtree(@p);
}

package OpenBSD::Error::catch;

sub exec($self, $fullerror, $error, $filename, $line)
{
	&$self();
}

1;
