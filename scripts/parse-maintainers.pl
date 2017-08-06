#!/usr/bin/perl -w

use strict;

my %hash;

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

sub alpha_output {
    foreach my $key (sort by_category keys %hash) {
	if ($key eq " ") {
	    chomp $hash{$key};
	    print $hash{$key};
	} else {
	    print "\n" . $key . "\n";
	    foreach my $pattern (sort by_pattern split('\n', $hash{$key})) {
		print($pattern . "\n");
	    }
	}
    }
}

sub trim {
    my $s = shift;
    $s =~ s/\s+$//;
    $s =~ s/^\s+//;
    return $s;
}

sub file_input {
    my $lastline = "";
    my $case = " ";
    $hash{$case} = "";

    while (<>) {
        my $line = $_;

        # Pattern line?
        if ($line =~ m/^([A-Z]):\s*(.*)/) {
            $line = $1 . ":\t" . trim($2) . "\n";
            if ($lastline eq "") {
                $hash{$case} = $hash{$case} . $line;
                next;
            }
            $case = trim($lastline);
            exists $hash{$case} and die "Header '$case' already exists";
            $hash{$case} = $line;
            $lastline = "";
            next;
        }

        if ($case eq " ") {
            $hash{$case} = $hash{$case} . $lastline;
            $lastline = $line;
            next;
        }
        trim($lastline) eq "" or die ("Odd non-pattern line '$lastline' for '$case'");
        $lastline = $line;
    }
    $hash{$case} = $hash{$case} . $lastline;
}

file_input();
alpha_output();

exit(0);
