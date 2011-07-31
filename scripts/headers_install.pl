#!/usr/bin/perl -w
#
# headers_install prepare the listed header files for use in
# user space and copy the files to their destination.
#
# Usage: headers_install.pl readdir installdir arch [files...]
# readdir:    dir to open files
# installdir: dir to install the files
# arch:       current architecture
#             arch is used to force a reinstallation when the arch
#             changes because kbuild then detect a command line change.
# files:      list of files to check
#
# Step in preparation for users space:
# 1) Drop all use of compiler.h definitions
# 2) Drop include of compiler.h
# 3) Drop all sections defined out by __KERNEL__ (using unifdef)

use strict;

my ($readdir, $installdir, $arch, @files) = @ARGV;

my $unifdef = "scripts/unifdef -U__KERNEL__ -D__EXPORTED_HEADERS__";

foreach my $file (@files) {
	my $tmpfile = "$installdir/$file.tmp";

	open(my $in, '<', "$readdir/$file")
	    or die "$readdir/$file: $!\n";
	open(my $out, '>', $tmpfile)
	    or die "$tmpfile: $!\n";
	while (my $line = <$in>) {
		$line =~ s/([\s(])__user\s/$1/g;
		$line =~ s/([\s(])__force\s/$1/g;
		$line =~ s/([\s(])__iomem\s/$1/g;
		$line =~ s/\s__attribute_const__\s/ /g;
		$line =~ s/\s__attribute_const__$//g;
		$line =~ s/^#include <linux\/compiler.h>//;
		$line =~ s/(^|\s)(inline)\b/$1__$2__/g;
		$line =~ s/(^|\s)(asm)\b(\s|[(]|$)/$1__$2__$3/g;
		$line =~ s/(^|\s|[(])(volatile)\b(\s|[(]|$)/$1__$2__$3/g;
		printf {$out} "%s", $line;
	}
	close $out;
	close $in;

	system $unifdef . " $tmpfile > $installdir/$file";
	unlink $tmpfile;
}
exit 0;
