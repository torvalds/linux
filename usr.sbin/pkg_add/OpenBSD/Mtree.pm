# ex:ts=8 sw=4:
# $OpenBSD: Mtree.pm,v 1.14 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2004-2005 Marc Espie <espie@openbsd.org>
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

package OpenBSD::Mtree;
use File::Spec;

# read an mtree file, and produce the corresponding directory hierarchy

sub parse_fh($mtree, $basedir, $fh, $h = undef)
{
	while(<$fh>) {
		chomp;
		s/^\s*//o;
		next if /^\#/o || /^\//o;
		s/\s.*$//o;
		next if /^$/o;
		if ($_ eq '..') {
			$basedir =~ s|/[^/]*$||o;
			next;
		} elsif (m/^\//) {
			$basedir = $_;
		} else {
			$basedir.="/$_";
		}
		$_ = $basedir;
		while (s|/\./|/|o)	{}
		if (defined $h) {
			$mtree->{File::Spec->canonpath($_)} //= {};
		} else {
			$mtree->{File::Spec->canonpath($_)} = 1;
		}
	}
}

sub parse($mtree, $basedir, $filename, $h = undef)
{
	open my $file, '<', $filename or die "can't open $filename: $!";
	parse_fh($mtree, $basedir, $file, $h);
	close $file;
}

1;
