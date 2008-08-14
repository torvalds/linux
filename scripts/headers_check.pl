#!/usr/bin/perl
#
# headers_check.pl execute a number of trivial consistency checks
#
# Usage: headers_check.pl dir [files...]
# dir:   dir to look for included files
# arch:  architecture
# files: list of files to check
#
# The script reads the supplied files line by line and:
#
# 1) for each include statement it checks if the
#    included file actually exists.
#    Only include files located in asm* and linux* are checked.
#    The rest are assumed to be system include files.
#
# 2) TODO: check for leaked CONFIG_ symbols

use strict;
use warnings;

my ($dir, $arch, @files) = @ARGV;

my $ret = 0;
my $line;
my $lineno = 0;
my $filename;

foreach my $file (@files) {
	$filename = $file;
	open(my $fh, '<', "$filename") or die "$filename: $!\n";
	$lineno = 0;
	while ($line = <$fh>) {
		$lineno++;
		check_include();
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
			$inc =~ s#asm/#asm-$arch/#;
			$found = stat($dir . "/" . $inc);
		}
		if (!$found) {
			printf STDERR "$filename:$lineno: included file '$inc' is not exported\n";
			$ret = 1;
		}
	}
}
