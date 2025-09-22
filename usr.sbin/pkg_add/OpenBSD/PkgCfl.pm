# ex:ts=8 sw=4:
# $OpenBSD: PkgCfl.pm,v 1.41 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2005 Marc Espie <espie@openbsd.org>
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
use warnings;

package OpenBSD::PkgCfl;
use OpenBSD::PackageName;
use OpenBSD::Search;
use OpenBSD::PackageInfo;

sub make_conflict_list($class, $plist)
{
	my $l = [];
	my $pkgname = $plist->pkgname;
	my $stem = OpenBSD::PackageName::splitstem($pkgname);

	unless (defined $plist->{'no-default-conflict'}) {
		push(@$l, OpenBSD::Search::PkgSpec->new("$stem-*", 1));
	} else {
		push(@$l, OpenBSD::Search::Exact->new($pkgname, 1));
	}
	if (defined $plist->{conflict}) {
		for my $cfl (@{$plist->{conflict}}) {
		    push(@$l, $cfl->spec);
		}
	}
	bless $l, $class;
}

sub conflicts_with($self, @pkgnames)
{
	my @libs = grep {/^\.libs\d*\-/} @pkgnames;
	@pkgnames = grep {!/^\.libs\d*\-/} @pkgnames;
	if (wantarray) {
		my @l = ();
		for my $cfl (@$self) {
			push(@l, $cfl->filter(@pkgnames));
			push(@l, $cfl->filter_libs(@libs));
		}
		return @l;
	} else {
		for my $cfl (@$self) {
			if ($cfl->filter(@pkgnames)) {
				return 1;
			}
			if ($cfl->filter_libs(@libs)) {
				return 1;
			}
		}
		return 0;
	}
}

sub register($plist, $state)
{
	$state->{conflict_list}{$plist->pkgname} = $plist->conflict_list;
}

sub unregister($plist, $state)
{
	delete $state->{conflict_list}{$plist->pkgname};
}

sub fill_conflict_lists($state)
{
	for my $pkg (installed_packages()) {
		my $plist = OpenBSD::PackingList->from_installation($pkg,
		    \&OpenBSD::PackingList::ConflictOnly);
		next unless defined $plist;
		if (!defined $plist->pkgname) {
			print STDERR "No pkgname in packing-list for $pkg\n";
			next;
		}
		register($plist, $state);
	}
}

sub find($pkgname, $state)
{
	my @bad = ();
	if (is_installed $pkgname) {
		push(@bad, $pkgname);
	}
	if (!defined $state->{conflict_list}) {
		fill_conflict_lists($state);
	}
	while (my ($name, $l) = each %{$state->{conflict_list}}) {
		next if $name eq $pkgname;
		if (!defined $l) {
			die "Error: $name has no definition";
		}
		if ($l->conflicts_with($pkgname)) {
			push(@bad, $name);
		}
	}
	return @bad;
}

sub find_all($plist, $state)
{
	my @first = $plist->conflict_list->conflicts_with(installed_packages());
	# XXX optimization
	if (@first > 0 && !$state->{allow_replacing}) {
		return @first;
	}

	my @conflicts = find($plist->pkgname, $state);
	return (@conflicts, @first);
}

1;
