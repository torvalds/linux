#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0

use strict;
use Getopt::Long qw(:config no_auto_abbrev);

my $input_file = "MAINTAINERS";
my $output_file = "MAINTAINERS.new";
my $output_section = "SECTION.new";
my $help = 0;

my $P = $0;

if (!GetOptions(
		'input=s' => \$input_file,
		'output=s' => \$output_file,
		'section=s' => \$output_section,
		'h|help|usage' => \$help,
	    )) {
    die "$P: invalid argument - use --help if necessary\n";
}

if ($help != 0) {
    usage();
    exit 0;
}

sub usage {
    print <<EOT;
usage: $P [options] <pattern matching regexes>

  --input => MAINTAINERS file to read (default: MAINTAINERS)
  --output => sorted MAINTAINERS file to write (default: MAINTAINERS.new)
  --section => new sorted MAINTAINERS file to write to (default: SECTION.new)

If <pattern match regexes> exist, then the sections that match the
regexes are not written to the output file but are written to the
section file.

EOT
}

# sort comparison functions
sub by_category($$) {
    my ($a, $b) = @_;

    $a = uc $a;
    $b = uc $b;

    # This always sorts last
    $a =~ s/THE REST/ZZZZZZ/g;
    $b =~ s/THE REST/ZZZZZZ/g;

    return $a cmp $b;
}

sub by_pattern($$) {
    my ($a, $b) = @_;
    my $preferred_order = 'MRPLSWTQBCFXNK';

    my $a1 = uc(substr($a, 0, 1));
    my $b1 = uc(substr($b, 0, 1));

    my $a_index = index($preferred_order, $a1);
    my $b_index = index($preferred_order, $b1);

    $a_index = 1000 if ($a_index == -1);
    $b_index = 1000 if ($b_index == -1);

    if (($a1 =~ /^F$/ && $b1 =~ /^F$/) ||
	($a1 =~ /^X$/ && $b1 =~ /^X$/)) {
	return $a cmp $b;
    }

    if ($a_index < $b_index) {
	return -1;
    } elsif ($a_index == $b_index) {
	return 0;
    } else {
	return 1;
    }
}

sub trim {
    my $s = shift;
    $s =~ s/\s+$//;
    $s =~ s/^\s+//;
    return $s;
}

sub alpha_output {
    my ($hashref, $filename) = (@_);

    return if ! scalar(keys %$hashref);

    open(my $file, '>', "$filename") or die "$P: $filename: open failed - $!\n";
    my $separator;
    foreach my $key (sort by_category keys %$hashref) {
	if ($key eq " ") {
	    print $file $$hashref{$key};
	} else {
	    if (! defined $separator) {
		$separator = "\n";
	    } else {
		print $file $separator;
	    }
	    print $file $key . "\n";
	    foreach my $pattern (sort by_pattern split('\n', %$hashref{$key})) {
		print $file ($pattern . "\n");
	    }
	}
    }
    close($file);
}

sub file_input {
    my ($hashref, $filename) = (@_);

    my $lastline = "";
    my $case = " ";
    $$hashref{$case} = "";

    open(my $file, '<', "$filename") or die "$P: $filename: open failed - $!\n";

    while (<$file>) {
        my $line = $_;

        # Pattern line?
        if ($line =~ m/^([A-Z]):\s*(.*)/) {
            $line = $1 . ":\t" . trim($2) . "\n";
            if ($lastline eq "") {
                $$hashref{$case} = $$hashref{$case} . $line;
                next;
            }
            $case = trim($lastline);
            exists $$hashref{$case} and die "Header '$case' already exists";
            $$hashref{$case} = $line;
            $lastline = "";
            next;
        }

        if ($case eq " ") {
            $$hashref{$case} = $$hashref{$case} . $lastline;
            $lastline = $line;
            next;
        }
        trim($lastline) eq "" or die ("Odd non-pattern line '$lastline' for '$case'");
        $lastline = $line;
    }
    $$hashref{$case} = $$hashref{$case} . $lastline;
    close($file);
}

my %hash;
my %new_hash;

file_input(\%hash, $input_file);

foreach my $type (@ARGV) {
    foreach my $key (keys %hash) {
	if ($key =~ /$type/ || $hash{$key} =~ /$type/) {
	    $new_hash{$key} = $hash{$key};
	    delete $hash{$key};
	}
    }
}

alpha_output(\%hash, $output_file);
alpha_output(\%new_hash, $output_section);

exit(0);
