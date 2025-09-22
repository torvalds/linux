#!/usr/bin/env perl
#
# Program:  findsym.pl
#
# Synopsis: Generate a list of the libraries in which a symbol is defined or
#           referenced.
#
# Syntax:   findsym.pl <directory_with_libraries_in_it> <symbol>
#

use warnings;

# Give first option a name.
my $Directory = $ARGV[0];
my $Symbol = $ARGV[1];

# Open the directory and read its contents, sorting by name and differentiating
# by whether its a library (.a) or an object file (.o)
opendir DIR,$Directory;
my @files = readdir DIR;
closedir DIR;
@objects = grep(/l?i?b?LLVM.*\.[oa]$/,sort(@files));

# Gather definitions from the libraries
foreach $lib (@objects) {
  my $head = 0;
  open SYMS, 
    "nm $Directory/$lib | grep '$Symbol' | sort --key=3 | uniq |";
  while (<SYMS>) {
    if (!$head) { print "$lib:\n"; $head = 1; }
    chomp($_);
    print "  $_\n";
  }
  close SYMS;
}
