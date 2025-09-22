# ex:ts=8 sw=4:
# $OpenBSD: OldLibs.pm,v 1.17 2023/06/13 09:07:17 espie Exp $
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

package OpenBSD::PackingElement;

# $self->mark_lib($libs, $libpatterns)
#	store libs into hashes
sub mark_lib($, $, $)
{
}

sub unmark_lib($, $, $)
{
}

# $self->separate_element($libs, $c1, $c2)
# 	based on libs hash, do we sort it into clone 1 or clone 2
sub separate_element($self, $, $, $c2)
{
	$c2->{$self} = 1;
}

sub special_deep_copy($self, $copy, $h, $)
{
	$self->clone->add_object($copy) if defined $h->{$self};
}

package OpenBSD::PackingElement::Meta;

# so every meta element ends up in both
sub separate_element($self, $, $c1, $c2)
{
	$c1->{$self} = 1;
	$c2->{$self} = 1;
}

package OpenBSD::PackingElement::DigitalSignature;

sub separate_element($self, $, $, $c2)
{
	$c2->{$self} = 1;
}

package OpenBSD::PackingElement::State;

sub separate_element	# forwarder
{
	&OpenBSD::PackingElement::Meta::separate_element;
}

package OpenBSD::PackingElement::Depend;
sub separate_element	# forwarder
{
	&OpenBSD::PackingElement::separate_element;
}

package OpenBSD::PackingElement::SpecialFile;
sub separate_element	# forwarder
{
	&OpenBSD::PackingElement::separate_element;
}

package OpenBSD::PackingElement::FCONTENTS;
sub special_deep_copy($, $, $, $)
{
}

package OpenBSD::PackingElement::Lib;
use File::Basename;

sub mark_lib($self, $libs, $libpatterns)
{
	my $libname = $self->fullname;
	my ($stem, $major, $minor, $dir) = $self->parse($libname);
	if (defined $stem) {
		$libpatterns->{$stem}->{$dir} = [$major, $minor, $libname];
	}
	$libs->{$libname} = 1;
}

sub separate_element($self, $libs, $c1, $c2)
{
	if ($libs->{$self->fullname}) {
		$c1->{$self} = 1;
	} else {
		$c2->{$self} = 1;
	}
}

sub unmark_lib($self, $libs, $libpatterns)
{
	my $libname = $self->fullname;
	my ($stem, $major, $minor, $dir) = $self->parse($libname);
	if (defined $stem) {
		my $p = $libpatterns->{$stem}->{$dir};
		if (defined $p && $p->[0] == $major && $p->[1] <= $minor) {
			my $n = $p->[2];
			delete $libs->{$n};
		}
	}
	delete $libs->{$libname};
}

sub enforce_dir($self, $path, $copy, $dirs)
{
	my $d = dirname($path);
	my $localbase = $copy->localbase;

	if ($d eq "$localbase/lib" || $d eq $localbase || $d eq '/') {
		return;
	}
	if ($dirs->{$d}) {
		return;
	}
	$dirs->{$d} = 1;
	$self->enforce_dir($d, $copy, $dirs);
	my $cwd = $self->cwd;
	$d =~ s/^\Q$cwd\E\///;
	OpenBSD::PackingElement::Dir->add($copy, $d);
}

sub special_deep_copy($self, $copy, $h, $dirs)
{
	$self->enforce_dir($self->fullname, $copy, $dirs);
	$self->SUPER::special_deep_copy($copy, $h, $dirs);
}

package OpenBSD::OldLibs;
use OpenBSD::RequiredBy;
use OpenBSD::PackageInfo;

sub split_some_libs($plist, $libs)
{
	my $c1 = {};
	my $c2 = {};
	$plist->separate_element($libs, $c1, $c2);
	my $p1 = OpenBSD::PackingList->new;
	$p1->set_infodir($plist->infodir);
	$plist->special_deep_copy($p1, $c1, {});
	my $p2 = $plist->make_shallow_copy($c2);
	return ($p1, $p2);
}

# create a packing-list with only the libraries we want to keep around.
sub split_libs($plist, $to_split)
{
	(my $splitted, $plist) = split_some_libs($plist, $to_split);

	require OpenBSD::PackageInfo;

	$splitted->set_pkgname(OpenBSD::PackageInfo::libs_package($plist->pkgname));

	if (defined $plist->{'no-default-conflict'}) {
		# we conflict with the package we just removed...
		OpenBSD::PackingElement::Conflict->add($splitted, $plist->pkgname);
	} else {
		my $stem = OpenBSD::PackageName::splitstem($plist->pkgname);
		OpenBSD::PackingElement::Conflict->add($splitted, $stem."-*");
	}
	return ($plist, $splitted);
}

sub adjust_depends_closure($oldname, $plist, $state)
{
	$state->say("    Packages that depend on those shared libraries:")
	    if $state->verbose >= 3;

	my $write = OpenBSD::RequiredBy->new($plist->pkgname);
	for my $pkg (OpenBSD::RequiredBy->compute_closure($oldname)) {
		$state->say("\t#1", $pkg) if $state->verbose >= 3;
		$write->add($pkg);
		my $r = OpenBSD::Requiring->new($pkg)->add($plist->pkgname);
		if ($oldname =~ m/^\.libs\d*\-/o) {
			$r->delete($oldname);
		}
	}
}

sub do_save_libs($o, $libs, $state)
{
	my $oldname = $o->pkgname;

	($o->{plist}, my $stub_list) = split_libs($o->plist, $libs);
	my $stub_name = $stub_list->pkgname;
	my $dest = installed_info($stub_name);
	$state->say("    (keeping them in #1)", $stub_name)
	    if $state->verbose >= 2;


	if ($state->{not}) {

		$state->shlibs->add_libs_from_plist($stub_list);
		$stub_list->to_cache;
		$o->plist->to_cache;
	} else {
		mkdir($dest);
		open my $descr, '>', $dest.DESC;
		print $descr "Stub libraries for $oldname\n";
		close $descr;
		my $f = OpenBSD::PackingElement::FDESC->add($stub_list, DESC);
		$f->{ignore} = 1;
		$f->add_digest($f->compute_digest($dest.DESC));
		$stub_list->to_installation;
		$o->plist->to_installation;
	}
	add_installed($stub_name);

	OpenBSD::PkgCfl::register($stub_list, $state);

	adjust_depends_closure($oldname, $stub_list, $state);
}

sub save_libs_from_handle($o, $set, $state)
{
	my $libs = {};
	my $p = {};

	$state->say("Looking for changes in shared libraries of #1",
	    $o->pkgname) if $state->verbose >= 2;
	$o->plist->mark_lib($libs, $p);
	for my $n ($set->newer) {
		$n->plist->unmark_lib($libs, $p);
	}
	for my $n ($set->kept) {
		$n->plist->unmark_lib($libs, $p);
	}

	if (%$libs) {
		$state->say("  ->Libraries to keep: #1",
		    join(",", sort(keys %$libs))) if $state->verbose >= 2;
		do_save_libs($o, $libs, $state);
	} else {
		$state->say("  ->No libraries to keep") if $state->verbose >= 2;
	}
}

sub save($self, $set, $state)
{
	for my $o ($set->older) {
		save_libs_from_handle($o, $set, $state);
	}
}

1;
