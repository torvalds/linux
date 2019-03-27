#!/usr/bin/perl -Tw
#-
# Copyright (c) 2002 Dag-Erling Coïdan Smørgrav
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
#      $FreeBSD$
#

use strict;
use Getopt::Std;

sub usage() {

    print(STDERR "usage: mtxstat [-gr] [-a|c|m|t] [-l limit]\n");
    exit(1);
}

MAIN:{
    my %opts;			# Command-line options
    my $key;			# Sort key
    my $limit;			# Output limit
    local *PIPE;		# Pipe
    my $header;			# Header line
    my @names;			# Field names
    my %data;			# Mutex data
    my @list;			# List of entries

    getopts("acgl:mrt", \%opts)
	or usage();
    if ($opts{'a'}) {
	usage()
	    if ($opts{'c'} || $opts{'m'} || $opts{'t'});
	$key = 'avg';
    } elsif ($opts{'c'}) {
	usage()
	    if ($opts{'m'} || $opts{'t'});
	$key = 'count';
    } elsif ($opts{'m'}) {
	usage()
	    if ($opts{'t'});
	$key = 'max';
    } elsif ($opts{'t'}) {
	$key = 'total';
    }
    if ($opts{'l'}) {
	if ($opts{'l'} !~ m/^\d+$/) {
	    usage();
	}
	$limit = $opts{'l'};
    }
    $ENV{'PATH'} = '/bin:/sbin:/usr/bin:/usr/sbin';
    open(PIPE, "sysctl -n debug.mutex.prof.stats|")
	or die("open(): $!\n");
    $header = <PIPE>;
    chomp($header);
    @names = split(' ', $header);
    if (defined($key) && !grep(/^$key$/, @names)) {
	die("can't find sort key '$key' in header\n");
    }
    while (<PIPE>) {
	chomp();
	my @fields = split(' ', $_, @names);
	next unless @fields;
	my %entry;
	foreach (@names) {
	    $entry{$_} = ($_ eq 'name') ? shift(@fields) : 0.0 + shift(@fields);
	}
	if ($opts{'g'}) {
	    $entry{'name'} =~ s/^(\S+)\s+\((.*)\)$/$2/;
	}
	my $name = $entry{'name'};
	if ($data{$name}) {
	    if ($entry{'max'} > $data{$name}->{'max'}) {
		$data{$name}->{'max'} = $entry{'max'};
	    }
	    $data{$name}->{'total'} += $entry{'total'};
	    $data{$name}->{'count'} += $entry{'count'};
	    $data{$name}->{'avg'} =
		$data{$name}->{'total'} / $data{$name}->{'count'};
	} else {
	    $data{$name} = \%entry;
	}
    }
    if (defined($key)) {
	@list = sort({ $data{$a}->{$key} <=> $data{$b}->{$key} }
		     sort(keys(%data)));
    } else {
	@list = sort(keys(%data));
    }
    if ($opts{'r'}) {
	@list = reverse(@list);
    }
    print("$header\n");
    if ($limit) {
	while (@list > $limit) {
	    pop(@list);
	}
    }
    foreach (@list) {
	printf("%6.0f %12.0f %11.0f %5.0f %-40.40s\n",
	       $data{$_}->{'max'},
	       $data{$_}->{'total'},
	       $data{$_}->{'count'},
	       $data{$_}->{'avg'},
	       $data{$_}->{'name'});
    }
}
