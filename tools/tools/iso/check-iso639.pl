#!/usr/bin/perl -w

#
# $FreeBSD$
#
# This script compares the file iso639 (from head/share/misc) with the file
# ISO-639-2_8859-1.txt (from
# http://www.loc.gov/standards/iso639-2/ISO-639-2_utf-8.txt) to see if there
# any differences.
#
# Created by Edwin Groothuis <edwin@FreeBSD.org> for the FreeBSD project.
#

use strict;
use Data::Dumper;

my %old = ();
{
	open(FIN, "iso639") or die "Cannot open iso639 (should be in head/share/misc)";
	my @lines = <FIN>;
	close(FIN);
	chomp(@lines);

	foreach my $l (@lines) {
		next if ($l =~ /^#/);
		next if ($l eq "");

		die "Bad line: $l\n"
			if ($l !~ /^([a-z\-]*)[ \t]+([a-z\-]+)[ \t]+([a-z\-]+)[ \t]+(.*)/);
		my $a2 = $1;
		my $bib = $2;
		my $term = $3;
		my $name = $4;

		$old{$bib}{a2} = $a2;
		$old{$bib}{bib} = $bib;
		$old{$bib}{term} = $term;
		$old{$bib}{name} = $name;
	}
}

my %new = ();
{
	open(FIN, "ISO-639-2_utf-8.txt") or die "Cannot open ISO-639-2_utf-8.txt, which can be retrieved from http://www.loc.gov/standards/iso639-2/ISO-639-2_utf-8.txt";
	my @lines = <FIN>;
	close(FIN);
	chomp(@lines);

	foreach my $l (@lines) {
		my @a = split(/\|/, $l);
		my $a2 = $a[2];
		my $bib = $a[0];
		my $term = $a[1];
		my $name = $a[3];

		$term = $bib if ($term eq "");

		$new{$bib}{a2} = $a2;
		$new{$bib}{bib} = $bib;
		$new{$bib}{term} = $term;
		$new{$bib}{name} = $name;
	}
}

{
	my $c = 0;
	foreach my $bib (sort(keys(%old))) {
		next if (defined $new{$bib});
		print "In old but not new: $old{$bib}{a2}\t$old{$bib}{bib}\t$old{$bib}{term}\t$old{$bib}{name}\n";
		$c++;
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $bib (sort(keys(%new))) {
		next if (defined $old{$bib});
		print "In new but not old: $new{$bib}{a2}\t$new{$bib}{bib}\t$new{$bib}{term}\t$new{$bib}{name}\n";
		$c++;
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $bib (sort(keys(%old))) {
		next if (!defined $new{$bib});
		next if ($old{$bib}{a2} eq $new{$bib}{a2} &&
			 $old{$bib}{bib} eq $new{$bib}{bib} &&
			 $old{$bib}{term} eq $new{$bib}{term} &&
			 $old{$bib}{name} eq $new{$bib}{name});
		print "In old: $old{$bib}{a2}\t$old{$bib}{bib}\t$old{$bib}{term}\t$old{$bib}{name}\n";
		print "In new: $new{$bib}{a2}\t$new{$bib}{bib}\t$new{$bib}{term}\t$new{$bib}{name}\n";
		$c++;
	}
	print "Found $c issues\n";
}
