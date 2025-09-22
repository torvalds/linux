# ex:ts=8 sw=4:
# $OpenBSD: IdCache.pm,v 1.12 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2002-2005 Marc Espie <espie@openbsd.org>
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

package OpenBSD::SimpleIdCache;
sub new($class)
{
	bless {}, $class;
}

sub lookup($self, $name, $default = undef)
{
	my $r;

	if (defined $self->{$name}) {
		$r = $self->{$name};
	} else {
		$r = $self->_convert($name);
		if (!defined $r) {
			$r = $default;
		}
		$self->{$name} = $r;
	}
	return $r;
}


package OpenBSD::IdCache;
our @ISA=qw(OpenBSD::SimpleIdCache);

sub lookup($self, $name, $default = undef)
{
	if ($name =~ m/^\d+$/o) {
		return $name;
	} else {
		return $self->SUPER::lookup($name, $default);
	}
}

package OpenBSD::UidCache;
our @ISA=qw(OpenBSD::IdCache);

sub _convert($, $key)
{
	my @entry = getpwnam($key);
	return @entry == 0 ? undef : $entry[2];
}

package OpenBSD::GidCache;
our @ISA=qw(OpenBSD::IdCache);

sub _convert($, $key)
{
	my @entry = getgrnam($key);
	return @entry == 0 ? undef : $entry[2];
}

package OpenBSD::UnameCache;
our @ISA=qw(OpenBSD::SimpleIdCache);

sub _convert($, $key)
{
	return getpwuid($key);
}

package OpenBSD::GnameCache;
our @ISA=qw(OpenBSD::SimpleIdCache);

sub _convert($, $key)
{
	return getgrgid($key);
}

1;
