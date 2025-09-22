# ex:ts=8 sw=4:
# $OpenBSD: AddCreateDelete.pm,v 1.57 2023/10/23 08:38:14 espie Exp $
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

# common behavior to pkg_add, pkg_delete, pkg_create

package OpenBSD::AddCreateDelete::State;
our @ISA = qw(OpenBSD::State);

use OpenBSD::State;
use OpenBSD::ProgressMeter;

sub init($self, @p)
{
	$self->{progressmeter} = OpenBSD::ProgressMeter->new;
	$self->{bad} = 0;
	$self->SUPER::init(@p);
	$self->{export_level}++;
}

sub progress($self)
{
	return $self->{progressmeter};
}

sub not($self)
{
	return $self->{not};
}

sub sync_display($self)
{
	$self->progress->clear;
}

sub add_interactive_options($self)
{
	$self->{has_interactive_options} = 1;
	return $self;
}

my $setup = {
	nowantlib => q'
	    	use OpenBSD::Dependencies::SolverBase;
		no warnings qw(redefine);
		package OpenBSD::Dependencies::SolverBase;
		sub solve_wantlibs($, $) { 1 }
	    ',
	nosystemwantlib => q'
	    	use OpenBSD::LibSpec;
		package OpenBSD::Library::System;
		sub no_match_dispatch($library, $spec, $base)
		{
			return $spec->no_match_name($library, $base);
		}
	    ',
	norun => q'
		package OpenBSD::State;
		sub _system(@) { 0 }
	    ',
};
		

sub handle_options($state, $opt_string, @usage)
{
	my $i;

	if ($state->{has_interactive_options}) {
		$opt_string .= 'iI';
		$state->{opt}{i} = sub() {
			$i++;
		};
	};

	$state->SUPER::handle_options($opt_string.'L:mnx', @usage);

	$state->progress->setup($state->opt('x'), $state->opt('m'), $state);
	$state->{not} = $state->opt('n');
	if ($state->{has_interactive_options}) {
		if ($state->opt('I')) {
			$i = 0;
		} elsif (!defined $i) {
			$i = -t STDIN;
		}
	}
	$state->{interactive} = $state->interactive_class($i)->new($state, $i);
	if ($state->defines('REGRESSION_TESTING')) {
		for my $i (split(/[,\s]/,
		    $state->defines('REGRESSION_TESTING'))) {
			$state->{regression}{$i} = 1;
			if (defined $setup->{$i}) {
				eval "$setup->{$i}";
				if ($@) {
					$state->fatal(
					    "Regression testing #1: #2", 
					    $i, $@);
				}
			}
		}
	}
}

sub interactive_class($, $i)
{
	if ($i) {
		require OpenBSD::Interactive;
		return 'OpenBSD::Interactive';
	} else {
		return 'OpenBSD::InteractiveStub';
	}
}

sub is_interactive($self)
{
	return $self->{interactive}->is_interactive;
}

sub find_window_size($state)
{
	$state->SUPER::find_window_size;
	$state->{progressmeter}->compute_playfield;
}

sub handle_continue($state)
{
	$state->SUPER::handle_continue;
	$state->{progressmeter}->handle_continue;
}

sub confirm_defaults_to_no($self, @p)
{
	return $self->{interactive}->confirm($self->f(@p), 0);
}

sub confirm_defaults_to_yes($self, @p)
{
	return $self->{interactive}->confirm($self->f(@p), 1);
}

sub ask_list($self, @p)
{
	return $self->{interactive}->ask_list(@p);
}

sub vsystem($self, @p)
{
	if ($self->verbose < 2) {
		$self->system(@p);
	} else {
		$self->verbose_system(@p);
	}
}

sub system($self, @p)
{
	$self->SUPER::system(@p);
}

sub run_makewhatis($state, $opts, $l)
{
	my $braindead = sub() { chdir('/'); };
	while (@$l > 1000) {
		my @b = splice(@$l, 0, 1000);
		$state->vsystem($braindead,
		    OpenBSD::Paths->makewhatis, @$opts, '--', @b);
	}
	$state->vsystem($braindead,
	    OpenBSD::Paths->makewhatis, @$opts, '--', @$l);
}

# TODO this stuff is definitely not as clear as it could be
sub ntogo($self, $offset = 0)
{
	return $self->{wantntogo} ?
	    $self->progress->ntogo($self, $offset) :
	    $self->f("ok");
}

sub ntogo_string($self, $offset = 0)
{
	return $self->{wantntogo} ?
	    $self->f(" (#1)", $self->ntodo($offset)) :
	    $self->f("");
}

sub solve_dependency($self, $solver, $dep, $package)
{
	return $solver->really_solve_dependency($self, $dep, $package);
}

package OpenBSD::AddCreateDelete;
use OpenBSD::Error;

sub handle_options($self, $opt_string, $state, @usage)
{
	$state->handle_options($opt_string, $self, @usage);
}

sub try_and_run_command($self, $state)
{
	if ($state->defines('pkg-debug')) {
		$self->run_command($state);
	} else {
		try {
			$self->run_command($state);
		} catch {
			$state->errsay("#1: #2", $state->{cmd}, $_);
			OpenBSD::Handler->reset;
			if ($_ =~ m/^Caught SIG(\w+)/o) {
				kill $1, $$;
			}
			$state->{bad}++;
		};
	}
}

package OpenBSD::InteractiveStub;
sub new($class, $, $)
{
	bless {}, $class;
}

sub ask_list($, $, @values)
{
	return $values[0];
}

sub confirm($, $, $yesno)
{
	return $yesno;
}

sub is_interactive($)
{
	return 0;
}
1;
