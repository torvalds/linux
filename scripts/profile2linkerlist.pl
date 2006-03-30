#!/usr/bin/perl

#
# Takes a (sorted) output of readprofile and turns it into a list suitable for
# linker scripts
#
# usage:
#	 readprofile | sort -rn | perl profile2linkerlist.pl > functionlist
#

while (<>) {
  my $line = $_;

  $_ =~ /\W*[0-9]+\W*([a-zA-Z\_0-9]+)\W*[0-9]+/;

  if ( ($line =~ /unknown/) || ($line =~ /total/)) {

  } else {
    print "*(.text.$1)\n";
  }
}
