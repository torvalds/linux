#!/usr/bin/perl -w

# Copyright 2008, Intel Corporation
#
# This file is part of the Linux kernel
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# Authors:
# 	Arjan van de Ven <arjan@linux.intel.com>


my $vmlinux_name = $ARGV[0];

#
# Step 1: Parse the oops to find the EIP value
#

my $target = "0";
while (<STDIN>) {
	if ($_ =~ /EIP: 0060:\[\<([a-z0-9]+)\>\]/) {
		$target = $1;
	}
}

if ($target =~ /^f8/) {
	print "This script does not work on modules ... \n";
	exit;
}

if ($target eq "0") {
	print "No oops found!\n";
	print "Usage: \n";
	print "    dmesg | perl scripts/markup_oops.pl vmlinux\n";
	exit;
}

my $counter = 0;
my $state   = 0;
my $center  = 0;
my @lines;

sub InRange {
	my ($address, $target) = @_;
	my $ad = "0x".$address;
	my $ta = "0x".$target;
	my $delta = hex($ad) - hex($ta);

	if (($delta > -4096) && ($delta < 4096)) {
		return 1;
	}
	return 0;
}



# first, parse the input into the lines array, but to keep size down,
# we only do this for 4Kb around the sweet spot

my $filename;

open(FILE, "objdump -dS $vmlinux_name |") || die "Cannot start objdump";

while (<FILE>) {
	my $line = $_;
	chomp($line);
	if ($state == 0) {
		if ($line =~ /^([a-f0-9]+)\:/) {
			if (InRange($1, $target)) {
				$state = 1;
			}
		}
	} else {
		if ($line =~ /^([a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9]+)\:/) {
			my $val = $1;
			if (!InRange($val, $target)) {
				last;
			}
			if ($val eq $target) {
				$center = $counter;
			}
		}
		$lines[$counter] = $line;

		$counter = $counter + 1;
	}
}

close(FILE);

if ($counter == 0) {
	print "No matching code found \n";
	exit;
}

if ($center == 0) {
	print "No matching code found \n";
	exit;
}

my $start;
my $finish;
my $codelines = 0;
my $binarylines = 0;
# now we go up and down in the array to find how much we want to print

$start = $center;

while ($start > 1) {
	$start = $start - 1;
	my $line = $lines[$start];
	if ($line =~ /^([a-f0-9]+)\:/) {
		$binarylines = $binarylines + 1;
	} else {
		$codelines = $codelines + 1;
	}
	if ($codelines > 10) {
		last;
	}
	if ($binarylines > 20) {
		last;
	}
}


$finish = $center;
$codelines = 0;
$binarylines = 0;
while ($finish < $counter) {
	$finish = $finish + 1;
	my $line = $lines[$finish];
	if ($line =~ /^([a-f0-9]+)\:/) {
		$binarylines = $binarylines + 1;
	} else {
		$codelines = $codelines + 1;
	}
	if ($codelines > 10) {
		last;
	}
	if ($binarylines > 20) {
		last;
	}
}


my $i;

my $fulltext = "";
$i = $start;
while ($i < $finish) {
	if ($i == $center) {
		$fulltext = $fulltext . "*$lines[$i]     <----- faulting instruction\n";
	} else {
		$fulltext = $fulltext .  " $lines[$i]\n";
	}
	$i = $i +1;
}

print $fulltext;

