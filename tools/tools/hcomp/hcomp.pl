#!/usr/bin/perl -w
#-
# Copyright (c) 2003 Dag-Erling Coïdan Smørgrav
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

our $opt_b;
our $opt_v;

sub hcomp($)
{
    my $fn = shift;

    local *FILE;
    my $header;

    warn("$fn\n")
	if ($opt_v);

    open(FILE, "<", $fn)
	or die("$fn: $!\n");
    $header = join('', <FILE>);
    close(FILE);

    # Remove comments
    $header =~ s|/\*.*?\*/||gs;
    $header =~ s|//.*$||gm;

    # Collapse preprocessor directives
    while ($header =~ s|(\n\#.*?)\\\n|$1|gs) {
	# nothing
    }

    # Remove superfluous whitespace
    $header =~ s|^\s+||s;
    $header =~ s|^\s+||gm;
    $header =~ s|\s+$||s;
    $header =~ s|\s+$||gm;
    $header =~ s|\n+|\n|gs;
    $header =~ s|[ \t]+| |gm;

    open(FILE, ">", "$fn.new")
	or die("$fn.new: $!\n");
    print(FILE $header);
    close(FILE);

    rename($fn, "$fn.$opt_b")
	if defined($opt_b);
    rename("$fn.new", $fn);
}

sub usage()
{
    print(STDERR "usage: hcomp [-b ext] file ...\n");
    exit(1);
}

MAIN:{
    my %opts;
    getopts('b:v')
	or usage();
    foreach (@ARGV) {
	hcomp($_);
    }
}
