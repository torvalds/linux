#!/usr/bin/perl -w
#
# whereintheworld
# Parses "make world" output and summarize where it's been so far.
#
# Bill Fenner <fenner@freebsd.org> 11 January 2000
# Dag-Erling Smørgrav <des@freebsd.org> 09 January 2003
#
# $Id: whereintheworld,v 1.3 2000/01/28 00:42:32 fenner Exp $
# $FreeBSD$
#

use strict;

my $line;
my $inside = 0;
my @lines = ();
my $thresh = 10;
my $lastwasdash = 0;
my $width = $ENV{COLUMNS} || 80;
my $error = 0;
my $elided = 0;

while ($line = <>) {
	if ($line =~ /^------------/) {
		$inside = !$inside;
		print $line unless ($lastwasdash);
		$lastwasdash = 1;
		@lines = ();
		next;
	}
	if ($inside && $line =~ /^>>>/) {
		print $line;
		$lastwasdash = 0;
		next;
	}
	if ($line =~ /^TB /) {
		print $line;
		next;
	}
	if ($line =~ /^=+>/) {
		@lines = ();
	}
	push(@lines, $line);
	if ($line =~ /^\*\*\* Error/ && $line !~ /\(ignored\)/) {
		$error = 1;
		while ($line = <>) {
			push(@lines, $line);
		}
		last;
	}
}

if (@lines && !$error) {
	print shift(@lines);
	while (@lines > $thresh) {
		shift(@lines);
		++$elided;
	}
	if ($elided > 0) {
		print "[$elided lines elided]\n";
	}
}
foreach $line (@lines) {
	if (!$error && $line !~ m/^TB / && length($line) >= $width) {
		substr($line, $width - 7) = " [...]\n";
	}
	print $line;
}
