#!/usr/bin/perl
#	$OpenBSD: remote.pl,v 1.4 2017/10/27 16:59:14 bluhm Exp $

# Copyright (c) 2010-2014 Alexander Bluhm <bluhm@openbsd.org>
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
use Cwd;
use File::Basename;

sub usage {
	die "usage: remote.pl remotessh args-test.pl\n";
}

@ARGV == 2 or usage();

my($remotessh, $testfile) = @ARGV;

my @opts = $ENV{SSH_OPTIONS} ? split(' ', $ENV{SSH_OPTIONS}) : ();
my $dir = dirname($0);
$dir = getcwd() if ! $dir || $dir eq ".";
my @cmd = ("ssh", "-n", @opts, $remotessh, "perl",
    "-I", "$dir/..", "$dir/error.pl", "$dir/".basename($testfile));
#print STDERR "execute: @cmd\n";
exec @cmd;
die "Exec '@cmd' failed: $!";
