# ex:ts=8 sw=4:
# $OpenBSD: Replace.pm,v 1.92 2023/06/13 09:07:17 espie Exp $
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

use OpenBSD::Delete;

package OpenBSD::PackingElement;
sub scan_for_exec($, $, $)
{
}

package OpenBSD::PackingElement::Exec;
sub scan_for_exec($self, $installing, $r)
{
	$$r = 1 if $installing;
}

package OpenBSD::PackingElement::ExecAdd;
sub scan_for_exec($, $, $) {}

package OpenBSD::PackingElement::Unexec;
sub scan_for_exec($self, $installing, $r)
{
	$$r = 1 if !$installing;
}

package OpenBSD::PackingElement::UnexecDelete;
sub scan_for_exec($, $, $) { }

package OpenBSD::Replace;

sub pkg_has_exec($pkg, $new)
{
	my $has_exec = 0;
	$pkg->plist->scan_for_exec($new, \$has_exec);
	return $has_exec;
}

sub set_has_no_exec($set, $state)
{
	for my $pkg ($set->older) {
		return 0 if pkg_has_exec($pkg, 0);
	}
	for my $pkg ($set->newer) {
		return 0 if pkg_has_exec($pkg, 1);
	}
	return 1;
}

1;
