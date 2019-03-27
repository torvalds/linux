#!/usr/bin/perl -w

#
# Copyright (C) 2009 Edwin Groothuis.  All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
# 
# $FreeBSD$
#

use strict;
use Data::Dumper;

if ($#ARGV < 0) {
	print <<EOF;
Usage: $0 -c <term1> <term2>
Compares the entries in the termcap.src for <term1> and <term2> and
print the keys and definitions on the screen. This can be used to reduce
the size of two similar termcap entries with the "tc" option.

Usage: $0 -l [term]
Show all lengths or the ones for terminals matching [term]

Usage: $0 -p <term>
Print all information about <term>

Usage: $0 -r <term>
Print all relations from and to <term>
EOF
	exit(0);
}

my $command = $ARGV[0];
my $tca = $ARGV[1];
my $tcb = $ARGV[2];

open(FIN, "termcap.src");
my @lines = <FIN>;
chomp(@lines);
close(FIN);

my %tcs = ();

my $tc = "";
foreach my $l (@lines) {
	next if ($l =~ /^#/);
	next if ($l eq "");

	$tc .= $l;
	next if ($l =~ /\\$/);

	$tc =~ s/:\\\s+:/:/g;

	my @a = split(/:/, $tc);
	next if ($#a < 0);
	my @b = split(/\|/, $a[0]);
	if ($#b >= 0) {
		$tcs{$b[0]} = $tc;
	} else {
		$tcs{$a[0]} = $tc;
	}
	if (length($tc) - length($a[0]) > 1023) {
		print "$a[0] has a length of ", length($tc) - length($a[0]), "\n";
		exit(0);
	}
	$tc = "";
}

my %tc = ();
my %keys = ();
my %len = ();
my %refs = ();

for my $tcs (keys(%tcs)) {
	$len{$tcs} = 0;
	my $first = 0;
	foreach my $tc (split(/:/, $tcs{$tcs})) {
		if ($first++ == 0) {
			foreach my $ref (split(/\|/, $tc)) {
				$refs{$ref} = $tcs;
			}
			next;
		}
		next if ($tc =~ /^\\/);
		$tc{$tcs}{$tc} = 0 if (!defined $tc{$tcs}{$tc});
		$tc{$tcs}{$tc}++;
		$len{$tcs} += length($tc) + 1;
		$keys{$tc} = 0;
	}
}

$tca = $refs{$tca} if (defined $tca && defined $refs{$tca});
$tcb = $refs{$tcb} if (defined $tcb && defined $refs{$tca});

die "Cannot find definitions for $tca" if (defined $tca && !defined $tcs{$tca});
die "Cannot find definitions for $tcb" if (defined $tcb && !defined $tcs{$tcb});

if ($command eq "-c") {
	foreach my $key (sort(keys(%keys))) {
		next if (!defined $tc{$tca}{$key} && !defined $tc{$tcb}{$key});
		printf("%-3s %-3s %s\n",
		    defined $tc{$tca}{$key} ? "+" : "",
		    defined $tc{$tcb}{$key} ? "+" : "",
		    $key,
		);
	}

	print "$len{$tca} - $len{$tcb}\n";
}

if ($command eq "-l") {
	foreach my $tcs (sort(keys(%tcs))) {
		next if (defined $tca && $tcs !~ /$tca/);
		printf("%4d %s\n", $len{$tcs}, $tcs);
	}
}

if ($command eq "-p") {
	printf("%s (%d bytes)\n", $tca, $len{$tca});
	foreach my $key (sort(keys(%keys))) {
		next if (!defined $tc{$tca}{$key});
		printf("%s\n", $key);
	}
}

if ($command eq "-r") {
	foreach my $key (keys(%{$tc{$tca}})) {
		next if ($key !~ /^tc=/);
		$key =~ s/tc=//;
		print "Links to:\t$key\n";
	}
	my $first = 0;
	foreach my $ref (sort(keys(%refs))) {
		next if ($refs{$ref} ne $tca);
		foreach my $tc (sort(keys(%tcs))) {
			if (defined $tc{$tc}{"tc=$ref"}) {
				if ($first++ == 0) {
					print "Links from:\t";
				} else {
					print "\t\t";
				}
				print "$ref -> $tc\n";
			}
		}
	}
}
