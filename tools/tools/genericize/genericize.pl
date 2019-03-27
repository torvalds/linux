#!/usr/bin/perl -w
#-
# Copyright (c) 2004 Dag-Erling Coïdan Smørgrav
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

use strict;
use Getopt::Std;

sub EMPTY() {}

MAIN:{
    my %opts;
    getopts('c', \%opts);

    my %config;
    my $machine;
    my $ident;

    while (<>) {
	chomp();
	s/\s*(\#.*)?$//;
	next unless $_;
	my ($keyword, $values) = split(' ', $_, 2);
	foreach my $value (split(/,\s*/, $values)) {
	    if ($keyword eq 'machine') {
		$machine = $value;
	    } elsif ($keyword eq 'ident') {
		$ident = $value;
	    } elsif ($keyword eq 'options' && $value =~ m/(\w+)=(.+)/) {
		$config{$keyword}->{$1} = $2;
	    } else {
		$config{$keyword}->{$value} = \&EMPTY;
	    }
	}
    }

    my $generic;
    if ($machine) {
	$generic = "/usr/src/sys/$machine/conf/GENERIC";
    } else {
	($generic = $ARGV) =~ s|([^/])+$|GENERIC|;
    }
    local *GENERIC;
    open(GENERIC, "<", $generic)
	or die("$generic: $!\n");
    my $blank = 0;
    while (<GENERIC>) {
	my $line = $_;
	chomp();
	if ($opts{'c'} && m/^\s*\#/) {
	    if ($blank) {
		print "\n";
		$blank = 0;
	    }
	    print $line;
	    next;
	}
	++$blank unless $_;
	s/\s*(\#.*)?$//;
	next unless $_;
	my ($keyword, $value) = split(' ', $_);
	if ($keyword eq 'machine') {
	    die("$generic is for $value, not $machine\n")
		unless ($value eq $machine);
	} elsif ($keyword eq 'ident') {
	    $line =~ s/$value/$ident/;
	} elsif ($keyword eq 'options' && $value =~ m/(\w+)=(.+)/ &&
	    $config{$keyword} && $config{$keyword}->{$1} &&
	    $config{$keyword}->{$1} != \&EMPTY) {
	    $value = $1;
	    if ($config{$keyword}->{$value} ne $2) {
		my ($old, $new) = ($2, $config{$keyword}->{$value});
		$line =~ s{=$old}{=$new};
	    }
	    delete($config{$keyword}->{$value});
	    delete($config{$keyword})
		unless %{$config{$keyword}};
	} elsif ($config{$keyword} && $config{$keyword}->{$value}) {
	    delete($config{$keyword}->{$value});
	    delete($config{$keyword})
		unless %{$config{$keyword}};
	} else {
	    next;
	}
	if ($blank) {
	    print "\n";
	    $blank = 0;
	}
	print $line;
    }
    close(GENERIC);

    if (%config) {
	print "\n# Addenda\n";
	foreach my $keyword (sort(keys(%config))) {
	    foreach my $value (sort(keys(%{$config{$keyword}}))) {
		print "$keyword";
		if (length($keyword) < 7) {
		    print "\t";
		} elsif (length($keyword) == 7) {
		    print " ";
		}
		print "\t$value";
		print "=$config{$keyword}->{$value}"
		    unless $config{$keyword}->{$value} == \&EMPTY;
		print "\n";
	    }
	}
    }
    exit(0);
}
