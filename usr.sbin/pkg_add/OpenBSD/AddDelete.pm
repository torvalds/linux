# ex:ts=8 sw=4:
# $OpenBSD: AddDelete.pm,v 1.101 2025/05/06 18:36:20 tb Exp $
#
# Copyright (c) 2007-2010 Marc Espie <espie@openbsd.org>
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

# common behavior to pkg_add / pkg_delete
package main;
our $not;

package OpenBSD::PackingElement::FileObject;
sub retrieve_fullname($self, $state, $pkgname)
{
	return $state->{destdir}.$self->fullname;
}

package OpenBSD::PackingElement::FileBase;
sub retrieve_size($self)
{
	return $self->{size};
}

package OpenBSD::PackingElement::SpecialFile;
use OpenBSD::PackageInfo;
sub retrieve_fullname($self, $state, $pkgname)
{
	return installed_info($pkgname).$self->name;
}

package OpenBSD::PackingElement::FCONTENTS;
sub retrieve_size($self)
{
	my $size = 0;
	my $cname = $self->fullname;
	if (defined $cname) {
		$size = (stat $cname)[7];
	}
	return $size;
}


package OpenBSD::AddDelete;
use OpenBSD::Error;
use OpenBSD::Paths;
use OpenBSD::PackageInfo;
use OpenBSD::AddCreateDelete;
our @ISA = qw(OpenBSD::AddCreateDelete);

sub do_the_main_work($self, $state)
{
	if ($state->{bad}) {
		return;
	}

	umask 0022;

	my $handler = sub { $state->fatal("Caught SIG#1", shift); };
	local $SIG{'INT'} = $handler;
	local $SIG{'QUIT'} = $handler;
	local $SIG{'HUP'} = $handler;
	local $SIG{'KILL'} = $handler;
	local $SIG{'TERM'} = $handler;

	if ($state->defines('pkg-debug')) {
		$self->main($state);
	} else {
		eval { $self->main($state); };
	}
	my $dielater = $@;
	return $dielater;
}

sub handle_end_tags($self, $state)
{
	return if !defined $state->{tags}{atend};
	$state->progress->for_list("Running tags", 
	    [keys %{$state->{tags}{atend}}],
	    sub($k) {
		return if $state->{tags}{deleted}{$k};
		return if $state->{tags}{superseded}{$k};
		$state->{tags}{atend}{$k}->run_tag($state);
	    });
}

sub run_command($self, $state)
{
	lock_db($state->{not}, $state) unless $state->defines('nolock');
	$state->check_root;
	$self->process_parameters($state);
	my $dielater = $self->do_the_main_work($state);
	# cleanup various things
	$self->handle_end_tags($state);
	$state->{recorder}->cleanup($state);
	$state->ldconfig->ensure;
	OpenBSD::PackingElement->finish($state);
	$state->progress->clear;
	$state->log->dump;
	$self->finish_display($state);
	if ($state->verbose >= 2 || $state->{size_only} ||
	    $state->defines('tally')) {
		$state->vstat->tally;
	}
	# show any error, and show why we died...
	rethrow $dielater;
}

sub parse_and_run($self, $cmd)
{
	my $state = $self->new_state($cmd);
	$state->handle_options;

	local $SIG{'INFO'} = sub { $state->status->print($state); };

	my ($lflag, $termios);
	if ($self->silence_children($state)) {
		require POSIX;

		$termios = POSIX::Termios->new;

		if (defined $termios->getattr) {
			$lflag = $termios->getlflag;
		}

		if (defined $lflag) {
			my $NOKERNINFO = 0x02000000; # not defined in POSIX
			$termios->setlflag($lflag | $NOKERNINFO);
			$termios->setattr;
		}
	}

	$self->try_and_run_command($state);

	if (defined $lflag) {
		$termios->setlflag($lflag);
		$termios->setattr;
	}

	return $self->exit_code($state);
}

sub exit_code($self, $state)
{
	return $state->{bad} != 0;
}

# $self->silence_children($state)
sub silence_children($, $)
{
	1;
}

# nothing to do
sub tweak_list($, $)
{
}

sub process_setlist($self, $state)
{
	$state->tracker->todo(@{$state->{setlist}});
	# this is the actual very small loop that processes all sets
	while (my $set = shift @{$state->{setlist}}) {
		$state->status->what->set($set);
		$set = $set->real_set;
		next if $set->{finished};
		$state->progress->set_header('Checking packages');
		unshift(@{$state->{setlist}}, $self->process_set($set, $state));
		$self->tweak_list($state);
	}
}

package OpenBSD::SharedItemsRecorder;
sub new($class)
{
	return bless {}, $class;
}

sub is_empty($self)
{
	return !(defined $self->{dirs} or defined $self->{users} or
	    defined $self->{groups});
}

sub cleanup($self, $state)
{
	return if $self->is_empty or $state->{not};

	require OpenBSD::SharedItems;
	OpenBSD::SharedItems::cleanup($self, $state);
}

package OpenBSD::AddDelete::State;
use OpenBSD::Vstat;
use OpenBSD::Log;
our @ISA = qw(OpenBSD::AddCreateDelete::State);

sub handle_options($state, $opt_string, @usage)
{
	$state->{extra_stats} = 0;
	$state->{opt}{V} = sub() {
		$state->{extra_stats}++;
	};
	$state->{no_exports} = 1;
	$state->add_interactive_options;
	$state->SUPER::handle_options($opt_string.'aciInqsVB:', @usage);

	if ($state->opt('s')) {
		$state->{not} = 1;
	}
	# XXX RequiredBy
	$main::not = $state->{not};
	$state->{localbase} = $state->opt('L') // OpenBSD::Paths->localbase;
	$ENV{PATH} = join(':',
	    '/bin',
	    '/sbin',
	    '/usr/bin',
	    '/usr/sbin',
	    '/usr/X11R6/bin',
	    "$state->{localbase}/bin",
	    "$state->{localbase}/sbin");

	$state->{size_only} = $state->opt('s');
	$state->{quick} = $state->opt('q');
	$state->{extra} = $state->opt('c');
	$state->{automatic} = $state->opt('a') // 0;
	$ENV{'PKG_DELETE_EXTRA'} = $state->{extra} ? "Yes" : "No";
	if ($state->{not} || $state->defines('DONTLOG')) {
		$state->{loglevel} = 0;
	}
	$state->{loglevel} //= 1;
	if ($state->{loglevel}) {
		require Sys::Syslog;
		Sys::Syslog::openlog($state->{cmd}, "nofatal");
	}
	$state->{wantntogo} = $state->{extra_stats};
	if (defined $ENV{PKG_CHECKSUM}) {
		$state->{subst}->add('checksum', 1);
	}
	my $base = $state->opt('B') // '';
	if ($base ne '') {
		$base.='/' unless $base =~ m/\/$/o;
	}
	$state->{destdir} = $base;
}

sub init($self, @p)
{
	$self->{l} = OpenBSD::Log->new($self);
	$self->{vstat} = OpenBSD::Vstat->new($self);
	$self->{status} = OpenBSD::Status->new;
	$self->{recorder} = OpenBSD::SharedItemsRecorder->new;
	$self->{v} = 0;
	$self->SUPER::init(@p);
	$self->{export_level}++;
}

sub syslog($self, @p)
{
	return unless $self->{loglevel};
	Sys::Syslog::syslog('info', $self->f(@p));
}

sub ntodo($state, $offset)
{
	return $state->tracker->sets_todo($offset);
}

# one-level dependencies tree, for nicer printouts
sub build_deptree($state, $set, @deps)
{
	if (defined $state->{deptree}{$set}) {
		$set = $state->{deptree}{$set};
	}
	for my $dep (@deps) {
		$state->{deptree}{$dep} = $set 
		    unless defined $state->{deptree}{$dep};
	}
}

sub deptree_header($state, $pkg)
{
	if (defined $state->{deptree}{$pkg}) {
		my $s = $state->{deptree}{$pkg}->real_set;
		if ($s eq $pkg) {
			delete $state->{deptree}{$pkg};
		} else {
			return $s->short_print.':';
		}
	}
	return '';
}

sub vstat($self)
{
	return $self->{vstat};
}

sub log($self, @p)
{
	if (@p == 0) {
		return $self->{l};
	} else {
		$self->{l}->say(@p);
	}
}

sub run_quirks($state, $sub)
{
	return if !$state->{uptodate_quirks};
	if (!exists $state->{quirks}) {
		eval {
			use lib ('/usr/local/libdata/perl5/site_perl');
			require OpenBSD::Quirks;
			# interface version number.
			$state->{quirks} = OpenBSD::Quirks->new(1);
		};
		if ($@ && !$state->{not}) {
			my $show = $state->verbose >= 2;
			if (!$show) {
				my $l = $state->repo->installed->match_locations(OpenBSD::Search::Stem->new('quirks'));
				$show = @$l > 0;
			}
			$state->errsay("Can't load quirk: #1", $@) if $show;
			# XXX cache that this didn't work
			$state->{quirks} = undef;
		}
	}

	if (defined $state->{quirks}) {
		eval {
			&$sub($state->{quirks});
		};
		if ($@) {
			$state->errsay("Bad quirk: #1", $@);
		}
	}
}

sub check_root($state)
{
	if ($< && !$state->defines('nonroot')) {
		if ($state->{not}) {
			$state->errsay("#1 should be run as root",
			    $state->{cmd}) if $state->verbose;
		} else {
			$state->fatal("#1 must be run as root", $state->{cmd});
		}
	}
}

sub choose_location($state, $name, $list, $is_quirks = 0)
{
	if (@$list == 0) {
		if (!$is_quirks) {
			$state->errsay("Can't find #1", $name);
			$state->run_quirks(
			    sub($quirks) {
				$quirks->filter_obsolete([$name], $state);
			    });
		}
		return undef;
	} elsif (@$list == 1) {
		return $list->[0];
	}

	my %h = map {($_->name, $_)} @$list;
	if ($state->is_interactive) {
		$h{'<None>'} = undef;
		$state->progress->clear;
		my $cmp = sub {		# XXX prototypable ?
			return -1 if !defined $h{$a};
			return 1 if !defined $h{$b};
			my $r = $h{$a}->pkgname->to_pattern cmp
				    $h{$b}->pkgname->to_pattern;
			if ($r == 0) {
				return $h{$a}->pkgname->{version}->
				    compare($h{$b}->pkgname->{version});
			} else {
				return $r;
			}
		};
		my $result = $state->ask_list("Ambiguous: choose package for $name", sort $cmp keys %h);
		return $h{$result};
	} else {
		$state->errsay("Ambiguous: #1 could be #2",
		    $name, join(' ', keys %h));
		return undef;
	}
}

sub status($self)
{
	return $self->{status};
}

sub replacing($self)
{
	return $self->{replacing};
}

OpenBSD::Auto::cache(ldconfig,
    sub($self) {
	return OpenBSD::LdConfig->new($self);
    });

# if we're not running as root, allow some stuff when not under /usr/local
sub allow_nonroot($state, $path)
{
	return $state->defines('nonroot') &&
	    $path !~ m,^\Q$state->{localbase}/\E,;
}

sub make_path($state, $path, $fullname)
{
	require File::Path;
	if ($state->allow_nonroot($fullname)) {
		eval {
			File::Path::mkpath($path);
		};
	} else {
		File::Path::mkpath($path);
	}
}

# this is responsible for running ldconfig when needed
package OpenBSD::LdConfig;

sub new($class, $state)
{
	bless { state => $state, todo => 0 }, $class;
}

# called once to figure out which directories are actually used
sub init($self)
{
	my $state = $self->{state};
	my $destdir = $state->{destdir};

	$self->{ldconfig} = [OpenBSD::Paths->ldconfig];

	$self->{path} = {};
	if ($destdir ne '') {
		unshift @{$self->{ldconfig}}, OpenBSD::Paths->chroot, '--',
		    $destdir;
	}
	open my $fh, "-|", @{$self->{ldconfig}}, "-r";
	if (defined $fh) {
		while (<$fh>) {
			if (m/^\s*search directories:\s*(.*?)\s*$/o) {
				for my $d (split(/\:/o, $1)) {
					$self->{path}{$d} = 1;
				}
				last;
			}
		}
		close($fh);
	} else {
		$state->errsay("Can't find ldconfig");
	}
}

# called from libs to figure out whether ldconfig should be rerun
sub mark_directory($self, $name)
{
	if (!defined $self->{path}) {
		$self->init;
	}
	require File::Basename;
	my $d = File::Basename::dirname($name);
	if ($self->{path}{$d}) {
		$self->{todo} = 1;
	}
}

# call before running any command (or at end) to run ldconfig just in time
sub ensure($self)
{
	if ($self->{todo}) {
		my $state = $self->{state};
		$state->vsystem(@{$self->{ldconfig}}, "-R")
		    unless $state->{not};
		$self->{todo} = 0;
	}
}

# the object that gets displayed during status updates
package OpenBSD::Status;

sub print($self, $state)
{
	my $what = $self->{what};
	$what //= 'Processing';
	my $object;
	if (defined $self->{object}) {
		$object = $self->{object};
	} elsif (defined $self->{set}) {
		$object = $self->{set}->print;
	} else {
		$object = "Parameters";
	}

	$state->say($what." #1#2", $object, $state->ntogo_string);
	if ($state->defines('carp')) {
		require Carp;
		Carp::cluck("currently here");
	}
}

sub set($self, $set)
{
	delete $self->{object};
	$self->{set} = $set;
	return $self;
}

sub object($self, $object)
{
	delete $self->{set};
	$self->{object} = $object;
	return $self;
}

sub what($self, $what = undef)
{
	$self->{what} = $what;
	return $self;
}

sub new($class)
{
	bless {}, $class;
}

1;
