#!/usr/bin/perl

my %include = ();
my %included = ();

for my $text (<*.txt>) {
    open I, '<', $text || die "cannot read: $text";
    while (<I>) {
	if (/^include::/) {
	    chomp;
	    s/^include::\s*//;
	    s/\[\]//;
	    $include{$text}{$_} = 1;
	    $included{$_} = 1;
	}
    }
    close I;
}

# Do we care about chained includes???
my $changed = 1;
while ($changed) {
    $changed = 0;
    while (my ($text, $included) = each %include) {
	for my $i (keys %$included) {
	    # $text has include::$i; if $i includes $j
	    # $text indirectly includes $j.
	    if (exists $include{$i}) {
		for my $j (keys %{$include{$i}}) {
		    if (!exists $include{$text}{$j}) {
			$include{$text}{$j} = 1;
			$included{$j} = 1;
			$changed = 1;
		    }
		}
	    }
	}
    }
}

while (my ($text, $included) = each %include) {
    if (! exists $included{$text} &&
	(my $base = $text) =~ s/\.txt$//) {
	print "$base.html $base.xml : ", join(" ", keys %$included), "\n";
    }
}
