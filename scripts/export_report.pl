#!/usr/bin/perl -w
#
# (C) Copyright IBM Corporation 2006.
#	Released under GPL v2.
#	Author : Ram Pai (linuxram@us.ibm.com)
#
# Usage: export_report.pl -k Module.symvers [-o report_file ] -f *.mod.c
#

use Getopt::Std;
use strict;

sub numerically {
	my $no1 = (split /\s+/, $a)[1];
	my $no2 = (split /\s+/, $b)[1];
	return $no1 <=> $no2;
}

sub alphabetically {
	my ($module1, $value1) = @{$a};
	my ($module2, $value2) = @{$b};
	return $value1 <=> $value2 || $module2 cmp $module1;
}

sub print_depends_on {
	my ($href) = @_;
	print "\n";
	while (my ($mod, $list) = each %$href) {
		print "\t$mod:\n";
		foreach my $sym (sort numerically @{$list}) {
			my ($symbol, $no) = split /\s+/, $sym;
			printf("\t\t%-25s\t%-25d\n", $symbol, $no);
		}
		print "\n";
	}
	print "\n";
	print "~"x80 , "\n";
}

sub usage {
        print "Usage: @_ -h -k Module.symvers  [ -o outputfile ] \n",
	      "\t-f: treat all the non-option argument as .mod.c files. ",
	      "Recommend using this as the last option\n",
	      "\t-h: print detailed help\n",
	      "\t-k: the path to Module.symvers file. By default uses ",
	      "the file from the current directory\n",
	      "\t-o outputfile: output the report to outputfile\n";
	exit 0;
}

sub collectcfiles {
        my @file = `cat .tmp_versions/*.mod | grep '.*\.ko\$'`;
        @file = grep {s/\.ko/.mod.c/} @file;
	chomp @file;
        return @file;
}

my (%SYMBOL, %MODULE, %opt, @allcfiles);

if (not getopts('hk:o:f',\%opt) or defined $opt{'h'}) {
        usage($0);
}

if (defined $opt{'f'}) {
	@allcfiles = @ARGV;
} else {
	@allcfiles = collectcfiles();
}

if (not defined $opt{'k'}) {
	$opt{'k'} = "Module.symvers";
}

unless (open(MODULE_SYMVERS, $opt{'k'})) {
	die "Sorry, cannot open $opt{'k'}: $!\n";
}

if (defined $opt{'o'}) {
	unless (open(OUTPUT_HANDLE, ">$opt{'o'}")) {
		die "Sorry, cannot open $opt{'o'} $!\n";
	}
	select OUTPUT_HANDLE;
}
#
# collect all the symbols and their attributes from the
# Module.symvers file
#
while ( <MODULE_SYMVERS> ) {
	chomp;
	my (undef, $symbol, $module, $gpl) = split;
	$SYMBOL { $symbol } =  [ $module , "0" , $symbol, $gpl];
}
close(MODULE_SYMVERS);

#
# collect the usage count of each symbol.
#
foreach my $thismod (@allcfiles) {
	unless (open(MODULE_MODULE, $thismod)) {
		print "Sorry, cannot open $thismod: $!\n";
		next;
	}
	my $state=0;
	while ( <MODULE_MODULE> ) {
		chomp;
		if ($state eq 0) {
			$state = 1 if ($_ =~ /static const struct modversion_info/);
			next;
		}
		if ($state eq 1) {
			$state = 2 if ($_ =~ /__attribute__\(\(section\("__versions"\)\)\)/);
			next;
		}
		if ($state eq 2) {
			if ( $_ !~ /0x[0-9a-f]{7,8},/ ) {
				next;
			}
			my $sym = (split /([,"])/,)[4];
			my ($module, $value, $symbol, $gpl) = @{$SYMBOL{$sym}};
			$SYMBOL{ $sym } =  [ $module, $value+1, $symbol, $gpl];
			push(@{$MODULE{$thismod}} , $sym);
		}
	}
	if ($state ne 2) {
		print "WARNING:$thismod is not built with CONFIG_MODVERSION enabled\n";
	}
	close(MODULE_MODULE);
}

print "\tThis file reports the exported symbols usage patterns by in-tree\n",
	"\t\t\t\tmodules\n";
printf("%s\n\n\n","x"x80);
printf("\t\t\t\tINDEX\n\n\n");
printf("SECTION 1: Usage counts of all exported symbols\n");
printf("SECTION 2: List of modules and the exported symbols they use\n");
printf("%s\n\n\n","x"x80);
printf("SECTION 1:\tThe exported symbols and their usage count\n\n");
printf("%-25s\t%-25s\t%-5s\t%-25s\n", "Symbol", "Module", "Usage count",
	"export type");

#
# print the list of unused exported symbols
#
foreach my $list (sort alphabetically values(%SYMBOL)) {
	my ($module, $value, $symbol, $gpl) = @{$list};
	printf("%-25s\t%-25s\t%-10s\t", $symbol, $module, $value);
	if (defined $gpl) {
		printf("%-25s\n",$gpl);
	} else {
		printf("\n");
	}
}
printf("%s\n\n\n","x"x80);

printf("SECTION 2:\n\tThis section reports export-symbol-usage of in-kernel
modules. Each module lists the modules, and the symbols from that module that
it uses.  Each listed symbol reports the number of modules using it\n");

print "~"x80 , "\n";
while (my ($thismod, $list) = each %MODULE) {
	my %depends;
	$thismod =~ s/\.mod\.c/.ko/;
	print "\t\t\t$thismod\n";
	foreach my $symbol (@{$list}) {
		my ($module, $value, undef, $gpl) = @{$SYMBOL{$symbol}};
		push (@{$depends{"$module"}}, "$symbol $value");
	}
	print_depends_on(\%depends);
}
