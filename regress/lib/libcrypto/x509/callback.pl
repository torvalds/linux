#!/usr/bin/perl
#
# Copyright (c) 2021 Bob Beck <beck@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

# Validate that we call out with the same chain on the callback with the legacy
# verifier as we do with the new verifier in compatibility mode. We ignore cases
# where one of the tests does not succesd to find a chain, as the error paths
# may be different. It's also ok for the new verifier to have signalled an
# error before finding a chain since it may try something and then back up.

my $Test;
my $State = "Read";
my @Legacy;
my @Modern;
my $Mfail;
my @Lfail;
my $Failures = "";

while (<>) {
    chomp;
    print "$_\n";
    if ($State eq "Read") {
	if (/^== Test/) {
	    $Test = $_;
	    @Legacy = ();
	    @Modern = ();
	    $Mfail = 0;
	    $Lfail = 0;
	    $State = "Read";
	    next;
	}
	if (/^== Legacy/) {
	    $State = "Legacy";
	    next;
	}
	if (/^== Modern/) {
	    $State = "Modern";
	    next;
	}
    }
    if ($State eq "Legacy") {
	if (/^INFO/) {
	    $State = "Read";
	    next;
	}
	if (/^FAIL/) {
	    $Lfail = 1;
	    $State = "Read";
	    next;
	}
	push @Legacy, ($_);
    }
    if ($State eq "Modern") {
	if (/^INFO/) {
	    $State = "Process";
	    next;
	}
	if (/^FAIL/) {
	    $Mfail = 1;
	    $State = "Process";
	    next;
	}
	push @Modern, ($_);
    }
    if ($State eq "Process") {
	my $mlen = scalar(@Modern);
	my $llen = scalar(@Legacy);
	print "$Test has $llen legacy lines and $mlen modern lines\n";
	while ($mlen > 0 && $llen > 0) {
	    my $lline = $Legacy[$llen - 1];
	    my $mline = $Modern[$mlen - 1];

	    if (!@Mfail && !$Lfail && $mline =~ /error 0 cert/ &&  $lline =~ /error 0 cert/) {
		if ($lline ne $mline) {
		    print "MISMATCH:  $lline VS $mline\n";
		    $Failures .= "$Test ";
		}
	    }
	    $mlen--;
	    $llen--;
	}
	$State = "Read";
	next;
    }
}
print "=============Test Summary============\n";
if ($Failures ne "") {
    print "FAIL: Mismatech on $Failures\n";
    exit 1;
}
print "Success: No Mismatches\n";
exit 0;
