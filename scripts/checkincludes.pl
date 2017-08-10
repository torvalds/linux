#!/usr/bin/env perl
#
# checkincludes: find/remove files included more than once
#
# Copyright abandoned, 2000, Niels Kristian Bech Jensen <nkbj@image.dk>.
# Copyright 2009 Luis R. Rodriguez <mcgrof@gmail.com>
#
# This script checks for duplicate includes. It also has support
# to remove them in place. Note that this will not take into
# consideration macros so you should run this only if you know
# you do have real dups and do not have them under #ifdef's. You
# could also just review the results.

use strict;

sub usage {
	print "Usage: checkincludes.pl [-r]\n";
	print "By default we just warn of duplicates\n";
	print "To remove duplicated includes in place use -r\n";
	exit 1;
}

my $remove = 0;

if ($#ARGV < 0) {
	usage();
}

if ($#ARGV >= 1) {
	if ($ARGV[0] =~ /^-/) {
		if ($ARGV[0] eq "-r") {
			$remove = 1;
			shift;
		} else {
			usage();
		}
	}
}

my $dup_counter = 0;

foreach my $file (@ARGV) {
	open(my $f, '<', $file)
	    or die "Cannot open $file: $!.\n";

	my %includedfiles = ();
	my @file_lines = ();

	while (<$f>) {
		if (m/^\s*#\s*include\s*[<"](\S*)[>"]/o) {
			++$includedfiles{$1};
		}
		push(@file_lines, $_);
	}

	close($f);

	if (!$remove) {
		foreach my $filename (keys %includedfiles) {
			if ($includedfiles{$filename} > 1) {
				print "$file: $filename is included more than once.\n";
				++$dup_counter;
			}
		}
		next;
	}

	open($f, '>', $file)
	    or die("Cannot write to $file: $!");

	my $dups = 0;
	foreach (@file_lines) {
		if (m/^\s*#\s*include\s*[<"](\S*)[>"]/o) {
			foreach my $filename (keys %includedfiles) {
				if ($1 eq $filename) {
					if ($includedfiles{$filename} > 1) {
						$includedfiles{$filename}--;
						$dups++;
						++$dup_counter;
					} else {
						print {$f} $_;
					}
				}
			}
		} else {
			print {$f} $_;
		}
	}
	if ($dups > 0) {
		print "$file: removed $dups duplicate includes\n";
	}
	close($f);
}

if ($dup_counter == 0) {
	print "No duplicate includes found.\n";
}
