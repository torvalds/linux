#!/usr/bin/perl -w

#
# $FreeBSD$
#
# This script compares the file iso3166 (from head/share/misc) with the files
# list-en1-semic-3.txt (from
# http://www.iso.org/iso/list-en1-semic-3.txt) and iso3166-countrycodes.txt
# (from ftp://ftp.ripe.net/) to see if there any differences.
#
# Created by Edwin Groothuis <edwin@FreeBSD.org> for the FreeBSD project.
#

use strict;
use Data::Dumper;

my %old = ();
{
	open(FIN, "iso3166") or die "Cannot open iso3166 (should be in head/share/misc)";
	my @lines = <FIN>;
	close(FIN);
	chomp(@lines);

	foreach my $l (@lines) {
		next if ($l =~ /^#/);
		next if ($l eq "");

		die "Bad line: $l\n"
			if ($l !~ /^([A-Z\-]*)[ \t]+([A-Z\-]+)[ \t]+(\d+)[ \t]+(.*)/);
		my $two = $1;
		my $three = $2;
		my $number = $3;
		my $name = $4;

		$old{$two}{two} = $two;
		$old{$two}{three} = $three;
		$old{$two}{number} = $number;
		$old{$two}{name} = $name;
	}
}

my %new1 = ();
{
	open(FIN, "iso3166-countrycodes.txt") or die "Cannot open iso3166-countrycodes.txt, which can be retrieved from ftp://ftp.ripe.net/iso3166-countrycodes.txt";
	my @lines = <FIN>;
	close(FIN);
	chomp(@lines);

	my $noticed = 0;
	foreach my $l (@lines) {
		if ($l =~ /\-\-\-\-\-\-\-/) {
			$noticed = 1;
			next;
		}
		next if (!$noticed);
		next if ($l eq "");

		die "Invalid line: $l\n"
			if ($l !~ /^(.+?)[\t ]+([A-Z]{2})[\t ]+([A-Z]{3})[\t ]+(\d+)[\t ]*$/);
		my $two = $2;
		my $three = $3;
		my $number = $4;
		my $name = $1;

		$new1{$two}{two} = $two;
		$new1{$two}{three} = $three;
		$new1{$two}{number} = $number;
		$new1{$two}{name} = $name;
	}
}

my %new2 = ();
{
	open(FIN, "list-en1-semic-3.txt") or die "Cannot open list-en1-semic-3.txt, which can be retrieved from http://www.iso.org/iso/list-en1-semic-3.txt";
	my @lines = <FIN>;
	close(FIN);
	chomp(@lines);

	my $noticed = 0;
	foreach my $l (@lines) {
		$l =~ s/\x0d//g;
		if (!$noticed) {	# skip the first line
			$noticed = 1;
			next;
		}
		next if ($l eq "");

		my @a = split(/;/, $l);
		die "Invalid line: $l\n" if ($#a != 1);
		my $two = $a[1];
		my $name = $a[0];

		$new2{$two}{two} = $two;
		$new2{$two}{name} = $name;
	}
}

{
	my $c = 0;
	foreach my $two (sort(keys(%old))) {
		if (!defined $new1{$two}) {
			print "In old but not new1: $old{$two}{two}\t$old{$two}{three}\t$old{$two}{number}\t$old{$two}{name}\n";
			$c++;
		}
		if (!defined $new2{$two}) {
			print "In old but not new2: $old{$two}{two}\t$old{$two}{name}\n";
			$c++;
		}
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $two (sort(keys(%new1))) {
		next if (defined $old{$two});
		print "In new1 but not old: $new1{$two}{two}\t$new1{$two}{three}\t$new1{$two}{number}\t$new1{$two}{name}\n";
		$c++;
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $two (sort(keys(%new2))) {
		next if (defined $old{$two});
		print "In new2 but not old: $new2{$two}{two}\t$new2{$two}{name}\n";
		$c++;
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $two (sort(keys(%old))) {
		if (defined $new1{$two}) {
			if ($old{$two}{two} ne $new1{$two}{two} ||
			    $old{$two}{three} ne $new1{$two}{three} ||
			    $old{$two}{number} ne $new1{$two}{number} ||
			    lc($old{$two}{name}) ne lc($new1{$two}{name})) {
				print "In old : $old{$two}{two}\t$old{$two}{three}\t$old{$two}{number}\t$old{$two}{name}\n";
				print "In new1: $new1{$two}{two}\t$new1{$two}{three}\t$new1{$two}{number}\t$new1{$two}{name}\n";
				$c++;
			}
		}
	}
	print "Found $c issues\n";
}

{
	my $c = 0;
	foreach my $two (sort(keys(%old))) {
		if (defined $new2{$two}) {
			if ($old{$two}{two} ne $new2{$two}{two} ||
			    lc($old{$two}{name}) ne lc($new2{$two}{name})) {
				print "In old : $old{$two}{two}\t$old{$two}{name}\n";
				print "In new2: $new2{$two}{two}\t$new2{$two}{name}\n";
				$c++;
			}
		}
	}
	print "Found $c issues\n";
}

