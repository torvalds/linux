#!/usr/bin/perl -w
#
# headers_check.pl execute a number of trivial consistency checks
#
# Usage: headers_check.pl dir arch [files...]
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
# 2) It is checked that prototypes does not use "extern"
#
# 3) Check for leaked CONFIG_ symbols

use strict;

my ($dir, $arch, @files) = @ARGV;

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
		&check_sizetypes();
		&check_declarations();
		# Dropped for now. Too much noise &check_config();
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

sub check_declarations
{
	if ($line =~m/^(\s*extern|unsigned|char|short|int|long|void)\b/) {
		printf STDERR "$filename:$lineno: " .
			      "userspace cannot reference function or " .
			      "variable defined in the kernel\n";
	}
}

sub check_config
{
	if ($line =~ m/[^a-zA-Z0-9_]+CONFIG_([a-zA-Z0-9_]+)[^a-zA-Z0-9_]/) {
		printf STDERR "$filename:$lineno: leaks CONFIG_$1 to userspace where it is not valid\n";
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
		"include of <linux/types.h> is preferred over <asm/types.h>\n"
		# Warn until headers are all fixed
		#$ret = 1;
	}
}

my $linux_types;
sub check_sizetypes
{
	if ($filename =~ /types.h|int-l64.h|int-ll64.h/o) {
		return;
	}
	if ($lineno == 1) {
		$linux_types = 0;
	} elsif ($linux_types >= 1) {
		return;
	}
	if ($line =~ m/^\s*#\s*include\s+<linux\/types.h>/) {
		$linux_types = 1;
		return;
	}
	if ($line =~ m/__[us](8|16|32|64)\b/) {
		printf STDERR "$filename:$lineno: " .
		              "found __[us]{8,16,32,64} type " .
		              "without #include <linux/types.h>\n";
		$linux_types = 2;
		# Warn until headers are all fixed
		#$ret = 1;
	}
}

