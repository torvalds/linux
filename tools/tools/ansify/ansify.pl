#!/usr/bin/perl -w
#-
# Copyright (c) 2005 Dag-Erling Coïdan Smørgrav
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
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

use v5.6.0;
use strict;

use Getopt::Long;

my $caddr_t;

sub ansify($$$) {
    my $ifh = shift;
    my $ofh = shift;
    my $fn = shift;

    my $line = 0;
  OUTER:
    while (<$ifh>) {
	# look for K&R-style function definitions
	if (m/^(\w+)\s*\(([\w,\s]+)\)$/) {
	    my @saved = ($_);
	    my $func = $1;
	    my @args = split(/\s*,\s*/, $2);
	    my $arglist = "";
	    # capture K&R-style argument list
	    while (<$ifh>) {
		push(@saved, $_);
		last if (m/^\{\s*$/);
		$arglist .= $_;
	    }
	    # remove comments (common in vfs code)
	    $arglist =~ s/\/\*([^*]|\*[^\/])*\*\// /gs;
	    # identify type of each argument
	    my %type;
	    foreach (split('\n', $arglist)) {
		s/\s+/ /g;
		if (!/^\s*([\w\s\*]+?)\s*(\w+?);\s*$/) {
		    warn("[$fn:$line] $func(): can't parse argument list\n");
		    print $ofh @saved;
		    $line += @saved;
		    next OUTER;
		}
		$type{$2} = $1;
	    }
	    # construct ANSI-style function definition
	    my $repl = "$func(";
	    foreach my $arg (@args) {
		# missing arguments in argument list
		if (!exists($type{$arg})) {
		    warn("[$fn:$line] $func(): unknown type for '$arg' argument\n");
		    print $ofh @saved;
		    $line += @saved;
		    next OUTER;
		}
		if ($caddr_t) {
		    $type{$arg} = "void *"
			if $type{$arg} eq "caddr_t";
		}
		$repl .= $type{$arg};
		$repl .= " "
		    unless ($type{$arg} =~ m/\*$/);
		$repl .= $arg;
		$repl .= ", "
		    unless $arg eq $args[-1];
		delete $type{$arg};
	    }
	    $repl .= ")";
	    # extra arguments in argument list
	    if (%type) {
		warn("[$fn:$line] $func(): too many arguments\n");
		print $ofh @saved;
		$line += @saved;
		next OUTER;
	    }
	    print $ofh "$repl\n";
	    ++$line;
	    # warn about long lines so they can be fixed up manually
	    warn("[$fn:$line] $func(): definition exceeds 80 characters\n")
		if length($repl) >= 80;
	    print $ofh "{\n";
	    ++$line;
	} else {
	    print $ofh $_;
	    ++$line;
	}
    }
}

sub ansify_file($) {
    my $fn = shift;

    my $tfn = "$fn.ansify";
    local *IN;
    local *OUT;

    if (open(IN, "<", $fn)) {
	if (open(OUT, ">", $tfn)) {
	    ansify(*IN{IO}, *OUT{IO}, $fn);
	    if (!rename($tfn, $fn)) {
		warn("$fn: $!\n");
		unlink($tfn);
	    }
	} else {
	    warn("$fn.ansify: $!\n");
	}
    } else {
	warn("$fn: $!\n");
    }
}

sub usage() {
    print STDERR "usage: ansify [options] [file ...]

Options:
  -c, --caddr_t                 Replace caddr_t with void * in converted
                                function definitions
";
    exit(1);
}

MAIN:{
    Getopt::Long::Configure("auto_abbrev", "bundling");
    GetOptions(
	       "c|caddr_t"		=> \$caddr_t,
	       )
	or usage();

    if (@ARGV) {
	foreach (@ARGV) {
	    ansify_file($_);
	}
    } else {
	ansify(*STDIN{IO}, *STDOUT{IO}, "(stdin)");
    }
}
