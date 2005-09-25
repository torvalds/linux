#!/usr/bin/perl -w
#
# reference_discarded.pl (C) Keith Owens 2001 <kaos@ocs.com.au>
#
# Released under GPL V2.
#
# List dangling references to vmlinux discarded sections.

use strict;
die($0 . " takes no arguments\n") if($#ARGV >= 0);

my %object;
my $object;
my $line;
my $ignore;
my $errorcount;

$| = 1;

# printf("Finding objects, ");
open(OBJDUMP_LIST, "find . -name '*.o' | xargs objdump -h |") || die "getting objdump list failed";
while (defined($line = <OBJDUMP_LIST>)) {
	chomp($line);
	if ($line =~ /:\s+file format/) {
		($object = $line) =~ s/:.*//;
		$object{$object}->{'module'} = 0;
		$object{$object}->{'size'} = 0;
		$object{$object}->{'off'} = 0;
	}
	if ($line =~ /^\s*\d+\s+\.modinfo\s+/) {
		$object{$object}->{'module'} = 1;
	}
	if ($line =~ /^\s*\d+\s+\.comment\s+/) {
		($object{$object}->{'size'}, $object{$object}->{'off'}) = (split(' ', $line))[2,5];
	}
}
close(OBJDUMP_LIST);
# printf("%d objects, ", scalar keys(%object));
$ignore = 0;
foreach $object (keys(%object)) {
	if ($object{$object}->{'module'}) {
		++$ignore;
		delete($object{$object});
	}
}
# printf("ignoring %d module(s)\n", $ignore);

# Ignore conglomerate objects, they have been built from multiple objects and we
# only care about the individual objects.  If an object has more than one GCC:
# string in the comment section then it is conglomerate.  This does not filter
# out conglomerates that consist of exactly one object, can't be helped.

# printf("Finding conglomerates, ");
$ignore = 0;
foreach $object (keys(%object)) {
	if (exists($object{$object}->{'off'})) {
		my ($off, $size, $comment, $l);
		$off = hex($object{$object}->{'off'});
		$size = hex($object{$object}->{'size'});
		open(OBJECT, "<$object") || die "cannot read $object";
		seek(OBJECT, $off, 0) || die "seek to $off in $object failed";
		$l = read(OBJECT, $comment, $size);
		die "read $size bytes from $object .comment failed" if ($l != $size);
		close(OBJECT);
		if ($comment =~ /GCC\:.*GCC\:/m || $object =~ /built-in\.o/) {
			++$ignore;
			delete($object{$object});
		}
	}
}
# printf("ignoring %d conglomerate(s)\n", $ignore);

# printf("Scanning objects\n");
$errorcount = 0;
foreach $object (keys(%object)) {
	my $from;
	open(OBJDUMP, "objdump -r $object|") || die "cannot objdump -r $object";
	while (defined($line = <OBJDUMP>)) {
		chomp($line);
		if ($line =~ /RELOCATION RECORDS FOR /) {
			($from = $line) =~ s/.*\[([^]]*).*/$1/;
		}
		if (($line =~ /\.text\.exit$/ ||
		     $line =~ /\.exit\.text$/ ||
		     $line =~ /\.data\.exit$/ ||
		     $line =~ /\.exit\.data$/ ||
		     $line =~ /\.exitcall\.exit$/) &&
		    ($from !~ /\.text\.exit$/ &&
		     $from !~ /\.exit\.text$/ &&
		     $from !~ /\.data\.exit$/ &&
		     $from !~ /\.exit\.data$/ &&
		     $from !~ /\.altinstructions$/ &&
		     $from !~ /\.pdr$/ &&
		     $from !~ /\.debug_.*$/ &&
		     $from !~ /\.exitcall\.exit$/ &&
		     $from !~ /\.eh_frame$/ &&
		     $from !~ /\.stab$/)) {
			printf("Error: %s %s refers to %s\n", $object, $from, $line);
			$errorcount = $errorcount + 1;
		}
	}
	close(OBJDUMP);
}
# printf("Done\n");

exit(0);
