#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0
# Prefix all lines with "# ", unbuffered. Command being piped in may need
# to have unbuffering forced with "stdbuf -i0 -o0 -e0 $cmd".
use strict;
use IO::Handle;

binmode STDIN;
binmode STDOUT;

STDOUT->autoflush(1);

my $needed = 1;
while (1) {
	my $char;
	my $bytes = sysread(STDIN, $char, 1);
	exit 0 if ($bytes == 0);
	if ($needed) {
		print "# ";
		$needed = 0;
	}
	print $char;
	$needed = 1 if ($char eq "\n");
}
