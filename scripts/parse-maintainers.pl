#!/usr/bin/perl -w

use strict;

my $P = $0;

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

    open(my $file, '>', "$filename") or die "$P: $filename: open failed - $!\n";
    foreach my $key (sort by_category keys %$hashref) {
	if ($key eq " ") {
	    chomp $$hashref{$key};
	    print $file $$hashref{$key};
	} else {
	    print $file "\n" . $key . "\n";
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

file_input(\%hash, "MAINTAINERS");

foreach my $type (@ARGV) {
    foreach my $key (keys %hash) {
	if ($key =~ /$type/ || $hash{$key} =~ /$type/) {
	    $new_hash{$key} = $hash{$key};
	    delete $hash{$key};
	}
    }
}

alpha_output(\%hash, "MAINTAINERS.new");
alpha_output(\%new_hash, "SECTION.new");

exit(0);
