#! /usr/bin/perl
# ex:ts=8 sw=4:
# $OpenBSD: Wrapper.pm,v 1.1 2019/07/09 13:49:47 espie Exp $
#
# Copyright (c) 2010 Marc Espie <espie@openbsd.org>
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

use strict;
use warnings;

# injection fault wrapper
package Wrapper;
require Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(wrap);

sub wrap
{
	my ($name, $sub) = @_;
	my $typeglob = caller()."::$name";
	my $original;
	{ 	
		no strict qw(refs);
		$original = *$typeglob{CODE};
	}
	my $imposter = sub {
		return &$sub($original, @_);
	};

	{ 
		no strict qw(refs);
		no warnings qw(redefine);
		*{$typeglob} = $imposter;
	}
}
1;
