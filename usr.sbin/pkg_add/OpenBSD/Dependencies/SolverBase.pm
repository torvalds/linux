# ex:ts=8 sw=4:
# $OpenBSD: SolverBase.pm,v 1.16 2023/06/13 09:07:18 espie Exp $
#
# Copyright (c) 2005-2018 Marc Espie <espie@openbsd.org>
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

# generic dependencies lookup class: walk the dependency tree as far
# as necessary to resolve dependencies
package OpenBSD::lookup;

# this is a template method that relies on subclasses defining
# find_in_already_done, find_in_extra_sources, find_in_new_source
# and find_elsewhere accordingly

sub lookup($self, $solver, $dependencies, $state, $obj)
{
	my $known = $self->{known};
	if (my $r = $self->find_in_already_done($solver, $state, $obj)) {
		$dependencies->{$r} = 1;
		return 1;
	}
	if ($self->find_in_extra_sources($solver, $state, $obj)) {
		return 1;
	}
	# lookup through the rest of the tree...
	my $done = $self->{done};

	while (my $dep = pop @{$self->{todo}}) {
		require OpenBSD::RequiredBy;

		next if $done->{$dep};
		# may need to replace older dep with newer ?
		my $newer = $self->may_adjust($solver, $state, $dep);
		if (defined $newer) {
			push(@{$self->{todo}}, $newer);
			next;
		}
		$done->{$dep} = 1;
		for my $dep2 (OpenBSD::Requiring->new($dep)->list) {
			push(@{$self->{todo}}, $dep2) unless $done->{$dep2};
		}
		$known->{$dep} = 1;
		if ($dep ne 'BaseSystem' && # XXX fake dependency
		    # updated package -> base system, don't bother looking
		    # (at this point there should be a fake handle but it's
		    # simpler to just test rather than retrofit everything)
		    $self->find_in_new_source($solver, $state, $obj, $dep)) {
			$dependencies->{$dep} = 2;
			return 1;
		}
	}
	if (my $r = $self->find_elsewhere($solver, $state, $obj)) {
		$dependencies->{$r} = 3;
		return 1;
	}

	return 0;
}

# While walking the dependency tree, we may loop back to an older package,
# because we're relying on dep lists on disk, that we haven't adjusted yet
# since we're just checking. We need to prepare for the update here as well!
sub may_adjust($self, $solver, $state, $dep)
{
	my $h = $solver->{set}{older}{$dep};
	if (defined $h) {
		$state->print("Detecting older #1...", $dep) 
		    if $state->verbose >=3;
		my $u = $h->{update_found};
		if (!defined $u) {
			$state->errsay("NO UPDATE FOUND for #1!", $dep);
		} elsif ($u->pkgname ne $dep) {
			$state->say("converting into #1", $u->pkgname) 
			    if $state->verbose >=3;
			return $u->pkgname;
		} else {
			$state->say("didn't change") 
			    if $state->verbose >=3;
		}	
	}
	return undef;
}

sub new($class, $solver)
{
	# prepare for closure
	my @todo = $solver->dependencies;
	bless { todo => \@todo, done => {}, known => {} }, $class;
}

sub dump($self, $state)
{
	return unless %{$self->{done}};
	$state->say("Full dependency tree is #1",
	    join(' ', keys %{$self->{done}}));
}

package OpenBSD::lookup::library;
our @ISA=qw(OpenBSD::lookup);

sub say_found($self, $state, $obj, $where)
{
	$state->say("found libspec #1 in #2", $obj->to_string, $where)
	    if $state->verbose >= 3;
}

sub find_in_already_done($self, $solver, $state, $obj)
{
	my $r = $solver->check_lib_spec($state, $solver->{localbase}, $obj,
	    $self->{known});
	if ($r) {
		$self->say_found($state, $obj, $state->f("package #1", $r));
		return $r;
	} else {
		return undef;
	}
}

sub find_in_extra_sources($self, $solver, $state, $obj)
{
	return undef if !$obj->is_valid || defined $obj->{dir};

	$state->shlibs->add_libs_from_system($state->{destdir});
	for my $dir ($state->shlibs->system_dirs) {
		if ($solver->check_lib_spec($state, $dir, $obj, {system => 1})) {
			$self->say_found($state, $obj, $state->f("#1/lib", $dir));
			return 'system';
		}
	}
	return undef;
}

sub find_in_new_source($self, $solver, $state, $obj, $dep)
{
	if (defined $solver->{set}{newer}{$dep}) {
		$state->shlibs->add_libs_from_plist($solver->{set}{newer}{$dep}->plist);
	} else {
		$state->shlibs->add_libs_from_installed_package($dep);
	}
	if ($solver->check_lib_spec($state, $solver->{localbase}, $obj, {$dep => 1})) {
		$self->say_found($state, $obj, $state->f("package #1", $dep));
		return $dep;
	}
	return undef;
}

sub find_elsewhere($self, $solver, $state, $obj)
{
	for my $n ($solver->{set}->newer) {
		for my $dep (@{$n->dependency_info->{depend}}) {
			my $r = $solver->find_old_lib($state,
			    $solver->{localbase}, $dep->{pattern}, $obj);
			if ($r) {
				$self->say_found($state, $obj,
				    $state->f("old package #1", $r));
				return $r;
			}
		}
	}
	return undef;
}

package OpenBSD::lookup::tag;
our @ISA=qw(OpenBSD::lookup);
sub new($class, $solver, $state)
{
	# prepare for closure
	if (!defined $solver->{old_dependencies}) {
		$solver->solve_old_depends($state);
	}
	my @todo = ($solver->dependencies, keys %{$solver->{old_dependencies}});
	bless { todo => \@todo, done => {}, known => {} }, $class;
}

sub find_in_extra_sources($, $, $, $)
{
}

sub find_elsewhere($, $, $, $)
{
}

sub find_in_already_done($self, $solver, $state, $obj)
{
	my $r = $self->{known_tags}{$obj->name};
	if (defined $r) {
		my ($dep, $d) = @$r;
		$obj->{definition_list} = $d;
		$state->say("Found tag #1 in #2", $obj->stringize, $dep)
		    if $state->verbose >= 3;
		return $dep;
	}
	return undef;
}

sub find_in_plist($self, $plist, $dep)
{
	if (defined $plist->{tags_definitions}) {
		while (my ($name, $d) = each %{$plist->{tags_definitions}}) {
			$self->{known_tags}{$name} = [$dep, $d];
		}
	}
}

sub find_in_new_source($self, $solver, $state, $obj, $dep)
{
	my $plist;

	if (defined $solver->{set}{newer}{$dep}) {
		$plist = $solver->{set}{newer}{$dep}->plist;
	} else {
		$plist = OpenBSD::PackingList->from_installation($dep,
		    \&OpenBSD::PackingList::DependOnly);
	}
	if (!defined $plist) {
		$state->errsay("Can't read plist for #1", $dep);
	}
	$self->find_in_plist($plist, $dep);
	return $self->find_in_already_done($solver, $state, $obj);
}


# both the solver and the conflict cache inherit from cloner
# they both want to merge several hashes from extra data.
package OpenBSD::Cloner;
sub clone($self, $h, @extra)
{
	for my $extra (@extra) {
		next unless defined $extra;
		while (my ($k, $e) = each %{$extra->{$h}}) {
			$self->{$h}{$k} //= $e;
		}
	}
}

# The actual solver derives from SolverBase:
# there is a specific subclass for pkg_create which does resolve
# dependencies in a much lighter way than the normal pkg_add code.
# (TODO: make it also work for tags!)
package OpenBSD::Dependencies::SolverBase;
our @ISA = qw(OpenBSD::Cloner);

my $global_cache = {};

sub cached($self, $dep)
{
	return $global_cache->{$dep->{pattern}} ||
	    $self->{cache}{$dep->{pattern}};
}

sub set_cache($self, $dep, $value)
{
	$self->{cache}{$dep->{pattern}} = $value;
}

sub set_global($self, $dep, $value)
{
	$global_cache->{$dep->{pattern}} = $value;
}

sub global_cache($self, $pattern)
{
	return $global_cache->{$pattern};
}

sub find_candidate($self, $dep, @list)
{
	my @candidates = $dep->spec->filter(@list);
	if (@candidates >= 1) {
		return $candidates[0];
	} else {
		return undef;
	}
}

sub solve_dependency($self, $state, $dep, $package)
{
	my $v;

	if (defined $self->cached($dep)) {
		if ($state->defines('stat_cache')) {
			if (defined $self->global_cache($dep->{pattern})) {
				$state->print("Global ");
			}
			$state->say("Cache hit on #1: #2", $dep->{pattern},
			    $self->cached($dep)->pretty);
		}
		$v = $self->cached($dep)->do($self, $state, $dep, $package);
		return $v if $v;
	}
	if ($state->defines('stat_cache')) {
		$state->say("No cache hit on #1", $dep->{pattern});
	}

	# we need an indirection because deleting is simpler
	$state->solve_dependency($self, $dep, $package);
}

sub solve_depends($self, $state)
{
	$self->{all_dependencies} = {};
	$self->{to_register} = {};
	$self->{deplist} = {};
	delete $self->{installed_list};

	for my $package ($self->{set}->newer, $self->{set}->kept) {
		$package->{before} = [];
		for my $dep (@{$package->dependency_info->{depend}}) {
			my $v = $self->solve_dependency($state, $dep, $package);
			# XXX
			next if !defined $v;
			$self->{all_dependencies}{$v} = $dep;
			$self->{to_register}{$package}{$v} = $dep;
		}
	}

	return sort values %{$self->{deplist}};
}

sub solve_wantlibs($solver, $state)
{
	my $okay = 1;

	my $lib_finder = OpenBSD::lookup::library->new($solver);
	for my $h ($solver->{set}->newer) {
		for my $lib (@{$h->{plist}->{wantlib}}) {
			$solver->{localbase} = $h->{plist}->localbase;
			next if $lib_finder->lookup($solver,
			    $solver->{to_register}{$h}, $state,
			    $lib->spec);
			if ($okay) {
				$solver->errsay_library($state, $h);
			}
			$okay = 0;
			$state->shlibs->report_problem($lib->spec);
		}
	}
	if (!$okay) {
		$solver->dump($state);
		$lib_finder->dump($state);
	}
	return $okay;
}

sub dump($self, $state)
{
	if ($self->dependencies) {
	    $state->print("Direct dependencies for #1 resolve to #2",
	    	$self->{set}->print, join(' ',  $self->dependencies));
	    $state->print(" (todo: #1)",
	    	join(' ', (map {$_->print} values %{$self->{deplist}})))
	    	if %{$self->{deplist}};
	    $state->print("\n");
	}
}

sub dependencies($self)
{
	if (wantarray) {
		return keys %{$self->{all_dependencies}};
	} else {
		return scalar(%{$self->{all_dependencies}});
	}
}

sub check_lib_spec($self, $state, $base, $spec, $dependencies)
{
	my $r = $state->shlibs->lookup_libspec($base, $spec);
	for my $candidate (@$r) {
		if ($dependencies->{$candidate->origin}) {
			return $candidate->origin;
		}
	}
	return;
}

sub find_dep_in_installed($self, $state, $dep)
{
	return $self->find_candidate($dep, @{$self->installed_list});
}

sub find_dep_in_self($self, $state, $dep)
{
	return $self->find_candidate($dep, $self->{set}->newer_names,
	    $self->{set}->kept_names);
}

sub find_in_self($solver, $plist, $state, $tag)
{
	return 0 unless defined $plist->{tags_definitions}{$tag->name};
	$tag->{definition_list} = $plist->{tags_definitions}{$tag->name};
	$tag->{found_in_self} = 1;
	$state->say("Found tag #1 in self", $tag->stringize)
	    if $state->verbose >= 3;
	return 1;
}

use OpenBSD::PackageInfo;
OpenBSD::Auto::cache(installed_list,
	sub($self) {
		my @l = installed_packages();

		for my $o ($self->{set}->older_names) {
			@l = grep {$_ ne $o} @l;
		}
		return \@l;
	}
);

sub add_dep($self, $d)
{
	$self->{deplist}{$d} = $d;
}


sub verify_tag($self, $tag, $state, $plist, $is_old)
{
	my $bad_return = $is_old ? 1 : 0;
	my $type = $is_old ? "Warning" : "Error";
	my $msg = "#1 in #2: \@tag #3";
	if (!defined $tag->{definition_list}) {
		$state->errsay("$msg definition not found",
		    $type, $plist->pkgname, $tag->name);
		return $bad_return;
	}
	my $use_params = 0;
	for my $d (@{$tag->{definition_list}}) {
		if ($d->need_params) {
			$use_params = 1;
			last;
		}
	}
	if ($tag->{params} eq '' && $use_params && !$tag->{found_in_self}) {
		$state->errsay(
		    "$msg has no parameters but some define wants them",
		    $type, $plist->pkgname, $tag->name);
		return $bad_return;
	} elsif ($tag->{params} ne '' && !$use_params) {
		$state->errsay(
		    "$msg has parameters but no define uses them",
		    $type, $plist->pkgname, $tag->name);
		return $bad_return;
	}
	return 1;
}

1;
