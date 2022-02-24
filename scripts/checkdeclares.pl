#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# checkdeclares: find struct declared more than once
#
# Copyright 2021 Wan Jiabing<wanjiabing@vivo.com>
# Inspired by checkincludes.pl
#
# This script checks for duplicate struct declares.
# Note that this will not take into consideration macros so
# you should run this only if you know you do have real dups
# and do not have them under #ifdef's.
# You could also just review the results.

use strict;

sub usage {
	print "Usage: checkdeclares.pl file1.h ...\n";
	print "Warns of struct declaration duplicates\n";
	exit 1;
}

if ($#ARGV < 0) {
	usage();
}

my $dup_counter = 0;

foreach my $file (@ARGV) {
	open(my $f, '<', $file)
	    or die "Cannot open $file: $!.\n";

	my %declaredstructs = ();

	while (<$f>) {
		if (m/^\s*struct\s*(\w*);$/o) {
			++$declaredstructs{$1};
		}
	}

	close($f);

	foreach my $structname (keys %declaredstructs) {
		if ($declaredstructs{$structname} > 1) {
			print "$file: struct $structname is declared more than once.\n";
			++$dup_counter;
		}
	}
}

if ($dup_counter == 0) {
	print "No duplicate struct declares found.\n";
}
