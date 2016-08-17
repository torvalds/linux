#!/usr/bin/perl -w

my $usage = <<EOT;
usage: config-enum enum [file ...]

Returns the elements from an enum declaration.

"Best effort": we're not building an entire C interpreter here!
EOT

use warnings;
use strict;
use Getopt::Std;

my %opts;

if (!getopts("", \%opts) || @ARGV < 1) {
	print $usage;
	exit 2;
}

my $enum = shift;

my $in_enum = 0;

while (<>) {
	# comments
	s/\/\*.*\*\///;
	if (m/\/\*/) {
		while ($_ .= <>) {
			last if s/\/\*.*\*\///s;
		}
	}

	# preprocessor stuff
	next if /^#/;

	# find our enum
	$in_enum = 1 if s/^\s*enum\s+${enum}(?:\s|$)//;
	next unless $in_enum;

	# remove explicit values
	s/\s*=[^,]+,/,/g;

	# extract each identifier
	while (m/\b([a-z_][a-z0-9_]*)\b/ig) {
		print $1, "\n";
	}

	#
	# don't exit: there may be multiple versions of the same enum, e.g.
	# inside different #ifdef blocks. Let's explicitly return all of
	# them and let external tooling deal with it.
	#
	$in_enum = 0 if m/}\s*;/;
}

exit 0;
