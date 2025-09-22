#!/usr/bin/perl -w

#
# Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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
#

#
# Ensure that the file offset for .text and .data aligns with the LMA. If they
# do not match boot(8) will fail since biosboot(8) does not perform relocation.
#

$LINKBASE = 0x40000;

if (scalar(@ARGV) != 1) {
	print "Usage: check-boot.pl boot\n";
	exit(2);
}

$boot = shift @ARGV;
if (! -f $boot) {
	print "No such file - $boot\n";
	exit(2);
}

$valid = 1;
$sections = 0;

open(OBJDUMP, "/usr/bin/objdump -h $boot |") || die "Failed to run objdump";
while (<OBJDUMP>) {
	@section = split / +/, $_;
	next if scalar(@section) < 7;
	if ($section[2] =~ /(\.(text|data))/) {
		$name = $1;
		$lma = hex($section[5]);
		$fileoff = hex($section[6]);
		$offset = $lma - $LINKBASE;
		$sections++;

		if ($fileoff != $offset) {
			printf "$name has incorrect file offset " .
			    "0x%x (should be 0x%x)\n", $fileoff, $offset;
			$valid = 0;
		}
	}
}
close(OBJDUMP);

if ($sections != 2) {
	print "Failed to find .text or .data section!\n";
	exit(1);
}

exit(1) if ! $valid;
