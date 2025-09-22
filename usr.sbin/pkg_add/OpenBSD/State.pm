# ex:ts=8 sw=4:
# $OpenBSD: State.pm,v 1.77 2023/11/25 10:18:40 espie Exp $
#
# Copyright (c) 2007-2014 Marc Espie <espie@openbsd.org>
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

package OpenBSD::PackageRepositoryFactory;
sub new($class, $state)
{
	return bless {state => $state}, $class;
}

sub locator($self)
{
	return $self->{state}->locator;
}

sub installed($self, $all = 0)
{
	require OpenBSD::PackageRepository::Installed;

	return OpenBSD::PackageRepository::Installed->new($all, $self->{state});
}

sub path_parse($self, $pkgname)
{
	return $self->locator->path_parse($pkgname, $self->{state});
}

sub find($self, $pkg)
{
	return $self->locator->find($pkg, $self->{state});
}

sub reinitialize($)
{
}

sub match_locations($self, @p)
{
	return $self->locator->match_locations(@p, $self->{state});
}

sub grabPlist($self, $url, $code)
{
	return $self->locator->grabPlist($url, $code, $self->{state});
}

sub path($self)
{
	require OpenBSD::PackageRepositoryList;

	return OpenBSD::PackageRepositoryList->new($self->{state});
}

# common routines to everything state.
# in particular, provides "singleton-like" access to UI.
package OpenBSD::State;
use OpenBSD::Subst;
use OpenBSD::Error;
use parent qw(OpenBSD::BaseState Exporter);
our @EXPORT = ();

sub locator($)
{
	require OpenBSD::PackageLocator;
	return "OpenBSD::PackageLocator";
}

sub cache_directory($)
{
	return undef;
}

sub new($class, $cmd = undef, @p)
{
	if (!defined $cmd) {
		$cmd = $0;
		$cmd =~ s,.*/,,;
	}
	my $o = bless {cmd => $cmd}, $class;
	$o->init(@p);
	return $o;
}

sub init($self)
{
	$self->{subst} = OpenBSD::Subst->new;
	$self->{repo} = OpenBSD::PackageRepositoryFactory->new($self);
	$self->{export_level} = 1;
	$SIG{'CONT'} = sub {
		$self->handle_continue;
	}
}

sub repo($self)
{
	return $self->{repo};
}

sub handle_continue($self)
{
	$self->find_window_size;
	# invalidate cache so this runs again after continue
	delete $self->{can_output};
}

OpenBSD::Auto::cache(can_output,
	sub($) {
		require POSIX;

		return 1 if !-t STDOUT;
		# XXX uses POSIX semantics so fd, we can hardcode stdout ;)
		my $s = POSIX::tcgetpgrp(1);
		# note that STDOUT may be redirected 
		# (tcgetpgrp() returns 0 for pipes and -1 for files)
		# (we shouldn't be there because of the tty test)
		return $s <= 0 || getpgrp() == $s;
	});

OpenBSD::Auto::cache(installpath,
	sub($self) {
		return undef if $self->defines('NOINSTALLPATH');
		require OpenBSD::Paths;
		open(my $fh, '<', OpenBSD::Paths->installurl) or return undef;
		while (<$fh>) {
			chomp;
			next if m/^\s*\#/;
			next if m/^\s*$/;
			return "$_/%c/packages/%a/";
		}
	});

OpenBSD::Auto::cache(shlibs,
	sub($self) {
		require OpenBSD::SharedLibs;
		return $self->{shlibs} //= OpenBSD::SharedLibs->new($self);
	});

sub usage_is($self, @usage)
{
	$self->{usage} = \@usage;
}

sub verbose($self)
{
	return $self->{v};
}

sub opt($self, $k)
{
	return $self->{opt}{$k};
}

sub usage($self, @p)
{
	my $code = 0;
	if (@p) {
		print STDERR "$self->{cmd}: ", $self->f(@p), "\n";
		$code = 1;
	}
	print STDERR "Usage: $self->{cmd} ", shift(@{$self->{usage}}), "\n";
	for my $l (@{$self->{usage}}) {
		print STDERR "       $l\n";
	}
	exit($code);
}

sub do_options($state, $sub)
{
	# this could be nicer...

	try {
		&$sub();
	} catch {
		$state->usage("#1", $_);
	};
}

sub validate_usage($state, $string, @usage)
{
	my $h = {};
	my $h2 = {};
	my $previous;
	for my $letter (split //, $string) {
		if ($letter eq ':') {
			$h->{$previous} = 1;
		} else {
			$previous = $letter;
			$h->{$previous} = 0;
		}
	}
	for my $u (@usage) {
		while ($u =~ s/\[\-(.*?)\]//) {
			my $opts = $1;
			if ($opts =~ m/^[A-Za-z]+$/) {
				for my $o (split //, $opts) {
					$h2->{$o} = 0;
				}
			} else {
				$opts =~ m/./;
				$h2->{$&} = 1;
			}
		}
	}
	for my $k (keys %$h) {
		if (!exists $h2->{$k}) {
			    $state->errsay("Option #1 #2is not in usage", $k,
				$h->{$k} ? "(with params) " : "");
		} elsif ($h2->{$k} != $h->{$k}) {
			$state->errsay("Discrepancy for option #1", $k);
		}
	}
	for my $k (keys %$h2) {
		if (!exists $h->{$k}) {
			$state->errsay("Option #1 does not exist", $k);
		}
	}
}

sub handle_options($state, $opt_string, @usage)
{
	require OpenBSD::Getopt;

	$state->{opt}{v} = 0 unless $opt_string =~ m/v/;
	$state->{opt}{h} = 
	    sub() { 
	    	$state->usage; 
	    } unless $opt_string =~ m/h/;
	$state->{opt}{D} = 
	    sub($opt) {
		$state->{subst}->parse_option($opt);
	    } unless $opt_string =~ m/D/;
	$state->usage_is(@usage);
	$state->do_options(sub() {
		OpenBSD::Getopt::getopts($opt_string.'hvD:', $state->{opt});
	});
	$state->{v} = $state->opt('v');

	# XXX don't try to move to AddCreateDelete, PkgInfo needs this too
	if ($state->defines('unsigned')) {
		$state->{signature_style} //= 'unsigned';
	} elsif ($state->defines('oldsign')) {
		$state->fatal('old style signature no longer supported');
	} else {
		$state->{signature_style} //= 'new';
	}

	if ($state->defines('VALIDATE_USAGE')) {
		$state->validate_usage($opt_string.'vD:', @usage);
	}
	return if $state->{no_exports};
	# TODO make sure nothing uses this
	no strict "refs";
	no strict "vars";
	for my $k (keys %{$state->{opt}}) {
		${"opt_$k"} = $state->opt($k);
		push(@EXPORT, "\$opt_$k");
	}
	local $Exporter::ExportLevel = $state->{export_level};
	OpenBSD::State->import;
}

sub defines($self, $k)
{
	return $self->{subst}->value($k);
}

sub width($self)
{
	if (!defined $self->{width}) {
		$self->find_window_size;
	}
	return $self->{width};
}

sub height($self)
{
	if (!defined $self->{height}) {
		$self->find_window_size;
	}
	return $self->{height};
}
		
sub find_window_size($self)
{
	require Term::ReadKey;
	my @l = Term::ReadKey::GetTermSizeGWINSZ(\*STDOUT);
	# default to sane values
	$self->{width} = 80;
	$self->{height} = 24;
	if (@l == 4) {
		# only use what we got if sane
		$self->{width} = $l[0] if $l[0] > 0;
		$self->{height} = $l[1] if $l[1] > 0;
		$SIG{'WINCH'} = sub {
			$self->find_window_size;
		};
	}
}

1;
