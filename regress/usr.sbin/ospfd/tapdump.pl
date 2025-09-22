#!/usr/bin/perl
#	$OpenBSD: tapdump.pl,v 1.1 2016/09/28 12:40:35 bluhm Exp $

# Copyright (c) 2014 Alexander Bluhm <bluhm@openbsd.org>
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
use Tap 'opentap';

my $tap = opentap(6)
    or die "Open tap device 6 failed: $!";

for (;;) {
    my $n = sysread($tap, my $buf, 70000);
    defined($n) or die "sysread failed: $!";
    $n or last;
    print "Read $n bytes\n";
    print unpack("H*", $buf), "\n";
}

print "Terminating\n"
