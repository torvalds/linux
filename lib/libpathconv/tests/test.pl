#!/usr/bin/perl
#
# Copyright (c) 1997 Shigio Yamaguchi. All rights reserved.
# Copyright (c) 1999 Tama Communications Corporation. All rights reserved.
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

#
# Test script for abs2rel(3) and rel2abs(3).
#
$logfile = 'err';
#
#       target          base directory  result
#       --------------------------------------
@abs2rel = (
	'.		/		.',
	'a/b/c		/		a/b/c',
	'a/b/c		/a		a/b/c',
	'/a/b/c		a		ERROR',
);
@rel2abs = (
	'.		/		/',
	'./		/		/',
	'/a/b/c		/		/a/b/c',
	'/a/b/c		/a		/a/b/c',
	'a/b/c		a		ERROR',
	'..		/a		/',
	'../		/a		/',
	'../..		/a		/',
	'../../		/a		/',
	'../../..	/a		/',
	'../../../	/a		/',
	'../b		/a		/b',
	'../b/		/a		/b/',
	'../../b	/a		/b',
	'../../b/	/a		/b/',
	'../../../b	/a		/b',
	'../../../b/	/a		/b/',
	'../b/c		/a		/b/c',
	'../b/c/	/a		/b/c/',
	'../../b/c	/a		/b/c',
	'../../b/c/	/a		/b/c/',
	'../../../b/c	/a		/b/c',
	'../../../b/c/	/a		/b/c/',
);
@common = (
	'/a/b/c		/a/b/c		.',
	'/a/b/c		/a/b/		c',
	'/a/b/c		/a/b		c',
	'/a/b/c		/a/		b/c',
	'/a/b/c		/a		b/c',
	'/a/b/c		/		a/b/c',
	'/a/b/c		/a/b/c		.',
	'/a/b/c		/a/b/c/		.',
	'/a/b/c/	/a/b/c		./',
	'/a/b/		/a/b/c		../',
	'/a/b		/a/b/c		..',
	'/a/		/a/b/c		../../',
	'/a		/a/b/c		../..',
	'/		/a/b/c		../../../',
	'/a/b/c		/a/b/z		../c',
	'/a/b/c		/a/y/z		../../b/c',
	'/a/b/c		/x/y/z		../../../a/b/c',
);
print "TEST start ";
open(LOG, ">$logfile") || die("cannot open log file '$logfile'.\n");
$cnt = 0;
$progname = 'abs2rel';
foreach (@abs2rel) {
	@d = split;
	chop($result = `./$progname $d[0] $d[1]`);
	if ($d[2] eq $result) {
		print '.';
	} else {
		print 'X';
		print LOG "$progname $d[0] $d[1] -> $result (It should be '$d[2]')\n";
		$cnt++;
	}
}
foreach (@common) {
	@d = split;
	chop($result = `./$progname $d[0] $d[1]`);
	if ($d[2] eq $result) {
		print '.';
	} else {
		print 'X';
		print LOG "$progname $d[0] $d[1] -> $result (It should be '$d[2]')\n";
		$cnt++;
	}
}
$progname = 'rel2abs';
foreach (@rel2abs) {
	@d = split;
	chop($result = `./$progname $d[0] $d[1]`);
	if ($d[2] eq $result) {
		print '.';
	} else {
		print 'X';
		print LOG "$progname $d[0] $d[1] -> $result (It should be '$d[2]')\n";
		$cnt++;
	}
}
foreach (@common) {
	@d = split;
	chop($result = `./$progname $d[2] $d[1]`);
	if ($d[0] eq $result) {
		print '.';
	} else {
		print 'X';
		print LOG "$progname $d[2] $d[1] -> $result (It should be '$d[0]')\n";
		$cnt++;
	}
}
close(LOG);
if ($cnt == 0) {
	print " COMPLETED.\n";
} else {
	print " $cnt errors detected.\n";
	open(LOG, $logfile) || die("log file not found.\n");
	while (<LOG>) {
		print;
	}
	close(LOG);
}
