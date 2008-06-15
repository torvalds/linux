#!/usr/bin/perl
#
# headers_install prepare the listed header files for use in
# user space and copy the files to their destination.
#
# Usage: headers_install.pl odir installdir [files...]
# odir:    dir to open files
# install: dir to install the files
# files:   list of files to check
#
# Step in preparation for users space:
# 1) Drop all use of compiler.h definitions
# 2) Drop include of compiler.h
# 3) Drop all sections defined out by __KERNEL__ (using unifdef)

use strict;
use warnings;

my ($readdir, $installdir, @files) = @ARGV;

my $unifdef = "scripts/unifdef -U__KERNEL__";

foreach my $file (@files) {
	my $tmpfile = "$installdir/$file.tmp";
	open(my $infile, '<', "$readdir/$file")
		or die "$readdir/$file: $!\n";
	open(my $outfile, '>', "$tmpfile") or die "$tmpfile: $!\n";
	while (my $line = <$infile>) {
		$line =~ s/([\s(])__user\s/$1/g;
		$line =~ s/([\s(])__force\s/$1/g;
		$line =~ s/([\s(])__iomem\s/$1/g;
		$line =~ s/\s__attribute_const__\s/ /g;
		$line =~ s/\s__attribute_const__$//g;
		$line =~ s/^#include <linux\/compiler.h>//;
		printf $outfile "%s", $line;
	}
	close $outfile;
	close $infile;
	system $unifdef . " $tmpfile > $installdir/$file";
	unlink $tmpfile;
}
exit 0;
