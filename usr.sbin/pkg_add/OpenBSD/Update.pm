# ex:ts=8 sw=4:
# $OpenBSD: Update.pm,v 1.172 2025/05/06 18:36:20 tb Exp $
#
# Copyright (c) 2004-2014 Marc Espie <espie@openbsd.org>
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

package OpenBSD::Handle;
sub update($self, $updater, $set, $state)
{

	return $updater->process_handle($set, $self, $state);
}

# TODO hint and hint2 are horrible names
package OpenBSD::hint;
sub update($self, $updater, $set, $state)
{

	return $updater->process_hint($set, $self, $state);
}

package OpenBSD::hint2;
sub update($self, $updater, $set, $state)
{
	return $updater->process_hint2($set, $self, $state);
}

package OpenBSD::Update;
use OpenBSD::PackageInfo;
use OpenBSD::PackageName;
use OpenBSD::Error;
use OpenBSD::UpdateSet;

sub new($class)
{
	return bless {}, $class;
}

sub add_handle($self, $set, $old, $n)
{
	$old->{update_found} = $n;
	$set->add_newer($n);
}

sub add_location($self, $set, $handle, $location)
{
	$self->add_handle($set, $handle,
	    OpenBSD::Handle->from_location($location));
}

sub look_for_debug($self, $set, $oldname, $newname, $state)
{
	# hurdles to pass before adding debug packages
	return unless $state->{debug_packages};

	return if $state->tracker->is_to_update("debug-".$oldname);
	my $dbg = "debug-".$newname;
	my $l = $set->match_locations(OpenBSD::Search::Exact->new($dbg));
	# TODO if @$l == 0, I should look for other packages with similar names
	# just so I can warn for out-of-date/shearing in the mirrors.
	return if @$l != 1;
	$set->add_newer(OpenBSD::Handle->from_location($l->[0]));
}

sub found_update($self, $set, $old, $location, $state)
{
	$self->add_location($set, $old, $location);
	$self->look_for_debug($set, $old->pkgname, $location->name, $state);
}

sub progress_message($self, $state, @r)
{
	my $msg = $state->f(@r);
	$msg .= $state->ntogo_string;
	$state->progress->message($msg);
	$state->say($msg) if $state->verbose >= 2;
}

sub process_handle($self, $set, $h, $state)
{
	my $pkgname = $h->pkgname;

	if ($pkgname =~ m/^\.libs\d*\-/o) {
		return 0;
	}

	my $base = 0;
	$state->run_quirks(
	    sub($quirks) {
		$base = $quirks->is_base_system($h, $state);
	    });
	if ($base) {
		$h->{update_found} = OpenBSD::Handle->system;
		$set->{updates}++;
		return 1;
	}

	my $plist = OpenBSD::PackingList->from_installation($pkgname,
	    \&OpenBSD::PackingList::UpdateInfoOnly);
	if (!defined $plist) {
		$state->fatal("can't locate #1", $pkgname);
	}

	if ($plist->has('firmware') && !$state->defines('FW_UPDATE')) {
		$set->move_kept($h);
		$h->{is_firmware} = 1;
		return 0;
	}

#	if (defined $plist->{url}) {
#		my $repo;
#		($repo, undef) = $state->repo->path_parse($plist->{url}->name);
#		$set->add_repositories($repo);
#	}
	my @search = ();

	my $sname = $pkgname;
	while ($sname =~ s/^partial\-//o) {
	}
	push(@search, OpenBSD::Search::Stem->split($sname));

	$state->run_quirks(
	    sub($quirks) {
		$quirks->tweak_search(\@search, $h, $state);
	    });
	my $oldfound = 0;
	my @skipped_locs = ();

	# XXX this is nasty: maybe we added an old set to update
	# because of conflicts, in which case the pkgpath +
	# conflict should be enough  to "match".
	for my $n ($set->newer) {
		if (($state->{hard_replace} ||
		    $n->location->update_info->match_pkgpath($plist)) &&
			$n->conflict_list->conflicts_with($sname)) {
				$self->add_handle($set, $h, $n);
				return 1;
		}
	}
	# XXX all that code conveniently forgets about old versions, while
	# marking them as "normal".
	# there should be some error path when we consistently fail to find
	# an equal-or-newer version in our repository, so that pkg_add has
	# consistent exit codes.
	if (!$state->defines('downgrade')) {
		push(@search, OpenBSD::Search::FilterLocation->more_recent_than($sname, \$oldfound));
	}
	push(@search, OpenBSD::Search::FilterLocation->new(
	    sub($l) {
		if (@$l == 0) {
			return $l;
		}
		my @l2 = ();
		for my $loc (@$l) {
		    if (!$loc) {
			    next;
		    }
		    my $p2 = $loc->update_info;
		    if (!$p2) {
			next;
		    }
		    if ($p2->has('arch')) {
			unless ($p2->{arch}->check($state->{arch})) {
			    $loc->forget;
			    next;
			}
		    }
		    if (!$plist->match_pkgpath($p2)) {
			push(@skipped_locs, $loc);
			next
		    }
		    my $r = $plist->signature->compare($p2->signature, $state);
		    if (defined $r && $r > 0 && !$state->defines('downgrade')) {
		    	$oldfound = 1;
			$loc->forget;
			next;
		    }
		    push(@l2, $loc);
		}
		return \@l2;
	    }));

	if (!$state->defines('allversions')) {
		push(@search, OpenBSD::Search::FilterLocation->keep_most_recent);
	}

	my $l = $set->match_locations(@search);

	for my $loc (@skipped_locs) {
		if (@$l == 0 && $state->verbose) {
			$self->say_skipped_packages($state, $plist,
				$loc->update_info);
		}
		$loc->forget;
	}

	if (@$l == 0) {
		if ($oldfound) {
			$set->move_kept($h);
			$self->progress_message($state,
			    "No need to update #1", $pkgname);
			$self->look_for_debug($set, $pkgname, $pkgname, $state);
			return 0;
		}
		return undef;
	}
	$state->say("Update candidates: #1 -> #2#3", $pkgname,
	    join(' ', map {$_->name} @$l), $state->ntogo_string) 
		if $state->verbose;

	my $r = $state->choose_location($pkgname, $l);
	if (defined $r) {
		$self->found_update($set, $h, $r, $state);
		return 1;
	} else {
		$state->{issues} = 1;
		return undef;
	}
}

sub say_skipped_packages($self, $state, $o, $n)
{
	my $o_name = $o->pkgname;
	my @o_ps = map { @{$o->pkgpath->{$_}} } keys %{$o->pkgpath};
	my $o_pp = join(" ", map {$_->fullpkgpath} @o_ps);

	my $n_name = $n->pkgname;
	my @n_ps = map { @{$n->pkgpath->{$_}} } keys %{$n->pkgpath};
	my $n_pp= join(" ", map {$_->fullpkgpath} @n_ps);

	my $t  = "Skipping #1 (update candidate for #2)";
	   $t .= "\n\t#2 pkgpaths: #4\n\t#1 pkgpaths: #3";

	$state->say($t, $n_name, $o_name, $n_pp, $o_pp);
}

sub find_nearest($base, $locs)
{
	my $pkgname = OpenBSD::PackageName->from_string($base);
	return undef if !defined $pkgname->{version};
	my @sorted = sort {$a->pkgname->{version}->compare($b->pkgname->{version}) } @$locs;
	if ($sorted[0]->pkgname->{version}->compare($pkgname->{version}) > 0) {
		return $sorted[0];
	}
	if ($sorted[-1]->pkgname->{version}->compare($pkgname->{version}) < 0) {
		return $sorted[-1];
	}
	return undef;
}

sub process_hint($self, $set, $hint, $state)
{
	my $l;
	my $hint_name = $hint->pkgname;
	my $k = OpenBSD::Search::FilterLocation->keep_most_recent;
	# first try to find us exactly

	$self->progress_message($state, "Looking for #1", $hint_name);
	$l = $set->match_locations(OpenBSD::Search::Exact->new($hint_name), $k);
	if (@$l == 0) {
		my $t = $hint_name;
		$t =~ s/\-\d([^-]*)\-?/--/;
		my @search = (OpenBSD::Search::Stem->new($t));
		$state->run_quirks(
		    sub($quirks) {
			$quirks->tweak_search(\@search, $hint, $state);
		    });
		$l = $set->match_locations(@search, $k);
	}
	if (@$l > 1) {
		my $r = find_nearest($hint_name, $l);
		if (defined $r) {
			$self->found_update($set, $hint, $r, $state);
			return 1;
		}
	}
	my $r = $state->choose_location($hint_name, $l);
	if (defined $r) {
		$self->found_update($set, $hint, $r, $state);
		OpenBSD::Add::tag_user_packages($set);
		return 1;
	} else {
		return 0;
	}
}

my $cache = {};

sub process_hint2($self, $set, $hint, $state)
{
	my $pkgname = $hint->pkgname;
	my $pkg2;
	if ($pkgname =~ m/[\/\:]/o) {
		my $repo;
		($repo, $pkg2) = $state->repo->path_parse($pkgname);
		$set->add_repositories($repo);
	} else {
		$pkg2 = $pkgname;
	}
	if (OpenBSD::PackageName::is_stem($pkg2)) {
		my $l = $state->updater->stem2location($set, $pkg2, $state,
		    $set->{quirks});
		if (defined $l) {
			$self->add_location($set, $hint, $l);
			$self->look_for_debug($set, $l->name, $l->name, $state);
		} else {
			return undef;
		}
	} else {
		if (!defined $cache->{$pkgname}) {
			$self->add_handle($set, $hint, OpenBSD::Handle->create_new($pkgname));
			$cache->{$pkgname} = 1;
			$pkg2 =~ s/\.tgz$//;
			$self->look_for_debug($set, $pkg2, $pkg2, $state);
		}
	}
	OpenBSD::Add::tag_user_packages($set);
	return 1;
}

sub process_set($self, $set, $state)
{
	my @problems = ();
	for my $h ($set->older, $set->hints) {
		next if $h->{update_found};
		if (!defined $h->update($self, $set, $state)) {
			push(@problems, $h->pkgname);
		}
	}
	if (@problems > 0) {
		$state->tracker->cant($set) if !$set->{quirks};
		if ($set->{updates} != 0) {
			$state->say("Can't update #1: no update found for #2",
			    $set->print, join(',', @problems));
		}
		return 0;
	} elsif ($set->{updates} == 0) {
		$state->tracker->uptodate($set);
		return 0;
	}
	$state->tracker->add_set($set);
	return 1;
}

sub stem2location($self, $locator, $name, $state, $is_quirks = 0)
{
	my $l = $locator->match_locations(OpenBSD::Search::Stem->new($name));
	if (@$l > 1 && !$state->defines('allversions')) {
		$l = OpenBSD::Search::FilterLocation->keep_most_recent->filter_locations($l);
	}
	return $state->choose_location($name, $l, $is_quirks);
}

1;
