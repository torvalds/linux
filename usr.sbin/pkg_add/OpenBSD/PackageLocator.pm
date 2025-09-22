# ex:ts=8 sw=4:
# $OpenBSD: PackageLocator.pm,v 1.111 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2010 Marc Espie <espie@openbsd.org>
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

package OpenBSD::PackageLocator;

use OpenBSD::PackageRepositoryList;
use OpenBSD::PackageRepository;

my $default_path;

sub add_default($self, $state, $p)
{
	my $w;

	if (defined $ENV{TRUSTED_PKG_PATH}) {
		my $v = $ENV{TRUSTED_PKG_PATH};
		$v =~ s/^\:+//o;
		$v =~ s/\:+$//o;
		while (my $o = OpenBSD::PackageRepository->parse(\$v, $state)) {
			$o->{trusted} = 1;
			$p->add($o);
		}
	} else {
		$w = "./:installpath";
	}
	if (defined $ENV{PKG_PATH}) {
		$w = $ENV{PKG_PATH};
	}
	if (defined $w) {
		$w =~ s/^\:+//o;
		$w =~ s/\:+$//o;
		while (my $o = OpenBSD::PackageRepository->parse(\$w, $state)) {
			$p->add($o);
		}
	}
}

sub build_default_path($self, $state)
{
	$default_path = OpenBSD::PackageRepositoryList->new($state);

	$self->add_default($state, $default_path);
}

sub default_path($self,$state)
{
	if (!defined $default_path) {
		$self->build_default_path($state);
	}
	return $default_path;
}

sub printable_default_path($self, $state)
{
	return join(':', $self->default_path($state)->do_something('url'));
}

sub path_parse($self, $pkgname, $state)
{
	my $path = './';
	if ($pkgname =~ m/^(.*[\/\:])(.*)/) {
		($pkgname, $path) = ($2, $1);
	}

	return (OpenBSD::PackageRepository->new($path, $state), $pkgname);
}

sub find($self, $url, $state)
{
	my $package;
	if ($url =~ m/[\/\:]/o) {
		my ($repository, $pkgname) = $self->path_parse($url, $state);
		$package = $repository->find($pkgname);
		if (defined $package) {
			$self->default_path($state)->add($repository);
		}
	} else {
		$package = $self->default_path($state)->find($url);
	}
	return $package;
}

sub grabPlist($self, $url, $code, $state)
{
	my $plist;
	if ($url =~ m/[\/\:]/o) {
		my ($repository, $pkgname) = $self->path_parse($url, $state);
		$plist = $repository->grabPlist($pkgname, $code);
		if (defined $plist) {
			$self->default_path($state)->add($repository);
		}
	} else {
		$plist = $self->default_path($state)->grabPlist($url, $code);
	}
	return $plist;
}

sub match_locations($self, @search)
{
	my $state = pop @search;
	return $self->default_path($state)->match_locations(@search);
}

1;
