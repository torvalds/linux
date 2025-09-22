# ex:ts=8 sw=4:
# $OpenBSD: CollisionReport.pm,v 1.49 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2006 Marc Espie <espie@openbsd.org>
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

use v5.36;

package OpenBSD::PackingElement;
sub handle_collisions($, $, $, $)
{
}

package OpenBSD::PackingElement::FileBase;
sub handle_collisions($self, $todo, $pkg, $bypkg)
{
	my $name = $self->fullname;
	if (defined $todo->{$name}) {
		push(@{$bypkg->{$pkg}}, $name);
		delete $todo->{$name};
	}
}

package OpenBSD::CollisionReport;
use OpenBSD::PackingList;
use OpenBSD::PackageInfo;

sub find_collisions($todo, $state)
{
	my $verbose = $state->verbose >= 3;
	my $bypkg = {};
	for my $name (keys %$todo) {
		my $pkg = $state->vstat->value($state->{destdir}.$name);
		if (defined $pkg) {
			push(@{$bypkg->{$pkg}}, $name);
			delete $todo->{$name};
		}
	}


	if (!%$todo) {
		return $bypkg;
	}
	for my $pkg (installed_packages()) {
		$state->say("Looking for collisions in #1", $pkg) if $verbose;
		# XXX in -n mode, some stuff is not really there
		# avoid warnings
		next unless -d installed_info($pkg);
		my $plist = OpenBSD::PackingList->from_installation($pkg,
		    \&OpenBSD::PackingList::FilesOnly);
		next if !defined $plist;
		$plist->handle_collisions($todo, $pkg, $bypkg);
	}
	return $bypkg;
}

sub collision_report($list, $state, $set)
{
	my $destdir = $state->{destdir};

	if ($state->defines('removecollisions')) {
		require OpenBSD::Error;
		for my $f (@$list) {
			$state->unlink(1, $destdir.$f->fullname);
		}
		return;
	}
	my %todo = map {($_->fullname, $_->{d})} @$list;
	my %extra = map {($_->fullname, $_->{newly_found})} @$list;
	my $clueless_bat;
	my $clueless_bat2;
	my $found = 0;

	$state->errsay("Collision in #1: the following files already exist",
	    $set->print);
	if (!$state->defines('dontfindcollisions')) {
		my $bypkg = find_collisions(\%todo, $state);
		for my $pkg (sort keys %$bypkg) {
		    for my $item (sort @{$bypkg->{$pkg}}) {
		    	$found++;
			$state->errsay("\t#1 (#2 and #3)", $item, $pkg,
			    $extra{$item});
		    }
		    if ($pkg =~ m/^(?:partial\-|borked\.\d+$)/o) {
			$clueless_bat = $pkg;
		    }
		    if ($pkg =~ m/^\.libs\d*-*$/o) {
			$clueless_bat2 = $pkg;
		    }
		}
	}
	if (%todo) {

		for my $item (sort keys %todo) {
			my $old = $todo{$item};
		    $state->errprint("\t#1 from #2", $item, $extra{$item});
		    if (defined $old && -f $destdir.$item) {
			    my $d = $old->new($destdir.$item);

			    if ($d->equals($old)) {
				    $state->errsay(" (same checksum)");
			    } else {
				    $state->errsay(" (different checksum)");
			    }
		    } else {
			    $state->errsay;
		    }
	    	}
	}
	if (defined $clueless_bat) {
		$state->errprint("The package name #1 suggests that a former installation\n".
		    "of a similar package got interrupted.  It is likely that\n".
		    "\tpkg_delete #1\n".
		    "will solve the problem\n", $clueless_bat);
	}
	if (defined $clueless_bat2) {
		$state->errprint("The package name #1 suggests remaining libraries\n".
		    "from a former package update.  It is likely that\n".
		    "\tpkg_delete #1\n".
		    "will solve the problem\n", $clueless_bat2);
	}
	my $dorepair = 0;
	if ($found == 0) {
		$dorepair = $state->defines('repair') ||
		    $state->confirm_defaults_to_no(
		    "It seems to be a missing package registration\nRepair");
	}
	if ($dorepair == 1) {
		for my $f (@$list) {

			if ($state->unlink($state->verbose >= 2,
			    $destdir.$f->fullname)) {
				$state->{problems}--;
			} else {
				return;
			}
		}
		$state->{repairdependencies} = 1;
	}
}

1;
