#!/usr/bin/perl

use warnings;
use strict;


# File::Slurp does things differently than a simple print $fh and $foo = <$fh>
# Only with File::Slurp, the issue caused by denode.h,v 1.31
# msdosfs_vnops.c,v 1.114 is reproducible with file_write_test().
# XXX One should do some ktrace analysis and rewrite the test to exactly do
# XXX what is required to trigger the issue.
use File::Slurp;

my $test = $ARGV[0] or usage();
my $basedir = $ARGV[1] or usage();

if ($test eq 'many_files_root') {
	many_files_test($basedir, 500);
} elsif ($test eq 'many_files_subdir') {
	my $dir = "$basedir/subdir";
	mkdir($dir) or die "could not create $dir";
	[ -d $dir ] or die "mkdir($dir) did not work?";
	many_files_test("$dir", 2000);
} elsif ($test eq 'file_write') {
	file_write_test("$basedir/file");
} else {
	usage();
}

exit 0;

### sub routines

# create lots of files in a dir an check that they can be read again
sub many_files_test {
	my $dir = shift;
	my $nfiles = shift;


	for my $i (1 .. $nfiles) {
		write_file("$dir/$i", "x$i\n")
		    or die "Could not writ file $dir/$i: $!";
	}

	foreach my $i (1 .. $nfiles) {
		my $file = "$dir/$i";
		my $content = read_file($file);
		defined $content or die "could not read $file: $!";
		if ($content ne "x$i\n") {
			die "$file has wrong content:'$content' instead of 'x$i\n'";
		}
		unlink($file) or die "could not unlink $file";
	}
	foreach my $i (1 .. $nfiles) {
		my $file = "$dir/$i";
		if (-e $file) {
			die "$file still exists?";
		}
	}
}

# create one ~ 500K file and check that reading gives the same contents
sub file_write_test {
	my $file = shift;
	my $content = '';

	for my $i (1 .. 100000) {
		$content .= "$i\n";
	}
	write_file($file, $content) or die "Could not write $file: $!";

	my $got = read_file($file) or die "Could not write $file: $!";
	if (length $got != length $content) {
		die "File $file is " . length $got . " bytes, expected "
		    . length $content;
	}

	if ($got ne $content) {
		my $i = 0;
		do {
			if (substr($got, $i, 1) ne substr($content, $i, 1)) {
				die "Got wrong content at pos $i in $file";
			}
			$i++;
		} while ($i < length($got));
		die "BUG";
	}

	unlink $file or die "can't delet $file: $!";
}

sub usage {
	print << "EOF";
usage: $0 <test> <mount-point>

	test can be one of:
		file_write
		many_files_root
		many_files_subdir
EOF
}
