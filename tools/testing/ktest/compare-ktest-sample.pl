#!/usr/bin/perl

open (IN,"ktest.pl");
while (<IN>) {
    if (/\$opt\{"?([A-Z].*?)(\[.*\])?"?\}/ ||
	/set_test_option\("(.*?)"/) {
	$opt{$1} = 1;
    }
}
close IN;

open (IN, "sample.conf");
while (<IN>) {
    if (/^\s*#?\s*(\S+)\s*=/) {
	$samp{$1} = 1;
    }
}
close IN;

foreach $opt (keys %opt) {
    if (!defined($samp{$opt})) {
	print "opt = $opt\n";
    }
}

foreach $samp (keys %samp) {
    if (!defined($opt{$samp})) {
	print "samp = $samp\n";
    }
}
