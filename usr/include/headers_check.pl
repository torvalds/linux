#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# headers_check.pl execute a number of trivial consistency checks
#
# Usage: headers_check.pl dir [files...]
# dir:   dir to look for included files
# files: list of files to check
#
# The script reads the supplied files line by line and:
#
# 1) for each include statement it checks if the
#    included file actually exists.
#    Only include files located in asm* and linux* are checked.
#    The rest are assumed to be system include files.
#
# 2) It is checked that prototypes does not use "extern"
#
# 3) Check for leaked CONFIG_ symbols

use warnings;
use strict;
use File::Basename;

my ($dir, @files) = @ARGV;

my $ret = 0;
my $line;
my $lineno = 0;
my $filename;

foreach my $file (@files) {
	$filename = $file;

	open(my $fh, '<', $filename)
		or die "$filename: $!\n";
	$lineno = 0;
	while ($line = <$fh>) {
		$lineno++;
		&check_include();
		&check_asm_types();
		&check_declarations();
	}
	close $fh;
}
exit $ret;

sub check_include
{
	if ($line =~ m/^\s*#\s*include\s+<((asm|linux).*)>/) {
		my $inc = $1;
		my $found;
		$found = stat($dir . "/" . $inc);
		if (!$found) {
			printf STDERR "$filename:$lineno: included file '$inc' is not exported\n";
			$ret = 1;
		}
	}
}

sub check_declarations
{
	# soundcard.h is what it is
	if ($line =~ m/^void seqbuf_dump\(void\);/) {
		return;
	}
	# drm headers are being C++ friendly
	if ($line =~ m/^extern "C"/) {
		return;
	}
	if ($line =~ m/^(\s*extern|unsigned|char|short|int|long|void)\b/) {
		printf STDERR "$filename:$lineno: " .
			      "userspace cannot reference function or " .
			      "variable defined in the kernel\n";
		$ret = 1;
	}
}

my $linux_asm_types;
sub check_asm_types
{
	if ($filename =~ /types.h|int-l64.h|int-ll64.h/o) {
		return;
	}
	if ($lineno == 1) {
		$linux_asm_types = 0;
	} elsif ($linux_asm_types >= 1) {
		return;
	}
	if ($line =~ m/^\s*#\s*include\s+<asm\/types.h>/) {
		$linux_asm_types = 1;
		printf STDERR "$filename:$lineno: " .
		"include of <linux/types.h> is preferred over <asm/types.h>\n";
		$ret = 1;
	}
}
