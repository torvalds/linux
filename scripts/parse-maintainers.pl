#!/usr/bin/perl -w

use strict;

my %map;

# sort comparison function
sub by_category($$) {
    my ($a, $b) = @_;

    $a = uc $a;
    $b = uc $b;

    # This always sorts last
    $a =~ s/THE REST/ZZZZZZ/g;
    $b =~ s/THE REST/ZZZZZZ/g;

    $a cmp $b;
}

sub alpha_output {
    my $key;
    my $sort_method = \&by_category;
    my $sep = "";

    foreach $key (sort $sort_method keys %map) {
        if ($key ne " ") {
            print $sep . $key . "\n";
            $sep = "\n";
        }
        print $map{$key};
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
    $map{$case} = "";

    while (<>) {
        my $line = $_;

        # Pattern line?
        if ($line =~ m/^([A-Z]):\s*(.*)/) {
            $line = $1 . ":\t" . trim($2) . "\n";
            if ($lastline eq "") {
                $map{$case} = $map{$case} . $line;
                next;
            }
            $case = trim($lastline);
            exists $map{$case} and die "Header '$case' already exists";
            $map{$case} = $line;
            $lastline = "";
            next;
        }

        if ($case eq " ") {
            $map{$case} = $map{$case} . $lastline;
            $lastline = $line;
            next;
        }
        trim($lastline) eq "" or die ("Odd non-pattern line '$lastline' for '$case'");
        $lastline = $line;
    }
    $map{$case} = $map{$case} . $lastline;
}

&file_input;
&alpha_output;
exit(0);
