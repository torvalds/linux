# ex:ts=8 sw=4:
# $OpenBSD: Tracker.pm,v 1.33 2023/10/08 09:16:39 espie Exp $
#
# Copyright (c) 2009 Marc Espie <espie@openbsd.org>
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

# In order to deal with dependencies, we have to know what's actually installed,
# and what can actually be updated.
# Specifically, to solve a dependency:
# - look at packages to_install
# - look at installed packages
#   - if it's marked to_update, then we must process the update first
#   - if it's marked as installed, or as cant_update, or uptodate, then
#   we can use the installed packages.
#   - otherwise, in update mode, put a request to update the package (e.g.,
#   create a new UpdateSet.

# the Tracker object does maintain that information globally so that
# Update/Dependencies can do its job.

use v5.36;
use warnings;

package OpenBSD::Tracker;

# XXX we're a singleton class
our $s;

sub new($class)
{
	return $s //= bless {}, $class;
}

sub dump2($set)
{
	if (defined $set->{merged}) {
		return "merged from ".dump2($set->{merged});
	}
	return join("/",
	    join(",", $set->newer_names), 
	    join(",", $set->older_names), 
	    join(",", $set->kept_names),
	    join(",", $set->hint_names));
}

sub dump($)
{
	return unless defined $s;
	for my $l ('to_install', 'to_update') {
		next unless defined $s->{$l};
		print STDERR "$l:\n";
		while (my ($k, $e) = each %{$s->{$l}}) {
			print STDERR "\t$k => ", dump2($e), "\n";
		}
	}
	for my $l ('uptodate', 'can_install', 'cant_update') {
		next unless defined $s->{$l};
		print STDERR "$l: ", join(' ', keys %{$s->{$l}}), "\n";
	}
}

sub sets_todo($self, $offset = 0)
{
	return sprintf("%u/%u", (scalar keys %{$self->{done}})-$offset,
		scalar keys %{$self->{total}});
}

sub handle_set($self, $set)
{
	$self->{total}{$set} = 1;
	if ($set->{finished}) {
		$self->{done}{$set} = 1;
	}
}

sub known($self, $set)
{
	for my $n ($set->newer, $set->older, $set->hints) {
		$self->{known}{$n->pkgname} = 1;
	}
}

sub add_set($self, $set)
{
	for my $n ($set->newer) {
		$self->{to_install}{$n->pkgname} = $set;
	}
	for my $n ($set->older, $set->hints) {
		$self->{to_update}{$n->pkgname} = $set;
	}
	for my $n ($set->kept) {
		delete $self->{to_update}{$n->pkgname};
		$self->{uptodate}{$n->pkgname} = 1;
		if ($n->{is_firmware}) {
			$self->{firmware}{$n->pkgname} = 1;
		}
	}
	$self->known($set);
	$self->handle_set($set);
	return $self;
}

sub todo($self, @sets)
{
	for my $set (@sets) {
		$self->add_set($set);
	}
	return $self;
}

sub remove_set($self, $set)
{
	for my $n ($set->newer) {
		delete $self->{to_install}{$n->pkgname};
		delete $self->{cant_install}{$n->pkgname};
	}
	for my $n ($set->kept, $set->older, $set->hints) {
		delete $self->{to_update}{$n->pkgname};
		delete $self->{cant_update}{$n->pkgname};
	}
	$self->handle_set($set);
}

sub uptodate($self, $set)
{
	$set->{finished} = 1;
	$self->remove_set($set);
	for my $n ($set->older, $set->kept) {
		$self->{uptodate}{$n->pkgname} = 1;
		if ($n->{is_firmware}) {
			$self->{firmware}{$n->pkgname} = 1;
		}
	}
}

sub cant($self, $set)
{
	$set->{finished} = 1;
	$self->remove_set($set);
	$self->known($set);
	for my $n ($set->older) {
		$self->{cant_update}{$n->pkgname} = 1;
	}
	for my $n ($set->newer) {
		$self->{cant_install}{$n->pkgname} = 1;
	}
	for my $n ($set->kept) {
		$self->{uptodate}{$n->pkgname} = 1;
	}
}

sub done($self, $set)
{
	$set->{finished} = 1;
	$self->remove_set($set);
	$self->known($set);

	for my $n ($set->newer) {
		$self->{uptodate}{$n->pkgname} = 1;
		$self->{installed}{$n->pkgname} = 1;
	}
	for my $n ($set->kept) {
		$self->{uptodate}{$n->pkgname} = 1;
	}
}

sub is($self, $k, $pkg)
{
	my $set = $self->{$k}{$pkg};
	if (ref $set) {
		return $set->real_set;
	} else {
		return $set;
	}
}

sub is_known($self, $pkg)
{
	return $self->is('known', $pkg);
}

sub is_installed($self, $pkg)
{
	return $self->is('installed', $pkg);
}

sub is_to_update($self, $pkg)
{
	return $self->is('to_update', $pkg);
}

sub cant_list($self)
{
	return keys %{$self->{cant_update}};
}

sub did_something($self)
{
	for my $k (keys %{$self->{uptodate}}) {
		next if $self->{firmware}{$k};
		return 1;
	}
	return 0;
}

sub cant_install_list($self)
{
	return keys %{$self->{cant_install}};
}

1;
