# ex:ts=8 sw=4:
# $OpenBSD: PackageRepositoryList.pm,v 1.34 2023/06/14 09:59:09 espie Exp $
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

package OpenBSD::PackageRepositoryList;

sub new($class, $state)
{
	return bless {l => [], k => {}, state => $state}, $class;
}

sub filter_new($self, @p)
{
	my @l = ();
	for my $r (@p) {
		next if !defined $r;
		next if $self->{k}{$r};
		$self->{k}{$r} = 1;
		push @l, $r;
	}
	return @l;
}

sub add($self, @p)
{
	push @{$self->{l}}, $self->filter_new(@p);
}

sub prepend($self, @p)
{
	unshift @{$self->{l}}, $self->filter_new(@p);
}

sub do_something($self, $do, $pkgname, @args)
{
	for my $repo (@{$self->{l}}) {
		my $r = $repo->$do($pkgname, @args);
		return $r if defined $r;
	}
	return undef;
}

sub find($self, @args)
{

	return $self->do_something('find', @args);
}

sub grabPlist($self, @args)
{
	return $self->do_something('grabPlist', @args);
}

sub match_locations($self, @search)
{
	my $result = [];
	for my $repo (@{$self->{l}}) {
		my $l = $repo->match_locations(@search);
		if ($search[0]->{keep_all}) {
			push(@$result, @$l);
		} elsif (@$l > 0) {
			return $l;
		}
	}
	return $result;
}

1;
