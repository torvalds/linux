#!/usr/bin/env perl
use strict;
use warnings;
require File::Temp;
use File::Temp ();

die "update_plist_test <test file> <plist file>\n" if ($#ARGV < 1);
my $testFile = shift @ARGV;
die "error: cannot read file $testFile\n" if (! -r $testFile);
my $plistFile = shift @ARGV;
die "error: cannot read file $plistFile\n" if (! -r $plistFile);

# Create a temp file for the new test.
my $fh = File::Temp->new();
my $filename = $fh->filename;
$fh->unlink_on_destroy(1);

# Copy the existing temp file, skipping the FileCheck comments.
open (IN, $testFile) or die "cannot open $testFile\n";
while (<IN>) {
  next if (/^\/\/ CHECK/);
  print $fh $_;
}
close(IN);

# Copy the plist data, and specially format it.
open (IN, $plistFile) or die "cannot open $plistFile\n";
my $firstArray = 1;
my $first = 1;
while (<IN>) {
  # Skip everything not indented.
  next if (/^[^\s]/);
  # Skip the first array entry, which is for files.
  if ($firstArray) {
    if (/<\/array>/) { $firstArray = 0; }
	next;
  }
  # Format the CHECK lines.
  if ($first) {
  	print $fh "// CHECK: ";
	$first = 0;
  }
  else {
  	print $fh "// CHECK-NEXT: ";
  }
  print $fh $_;
}
close (IN);
close ($fh);

`cp $filename $testFile`;
print "updated $testFile\n";
