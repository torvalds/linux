#!/usr/bin/perl -w

use strict;

# $FreeBSD$

# [1360537229.753890] [100494] TXD
# [1360537229.754292] [100494] TXSTATUS: TxDone=1, TS=0x5ccfa5c7

my ($tv_sec) = 0;
my ($tv_usec) = 0;

sub tvdiff($$$$) {
	my ($tv1_sec, $tv1_usec, $tv2_sec, $tv2_usec) = @_;

	if ($tv2_usec < $tv1_usec) {
		$tv2_usec += 1000000;
		$tv1_sec = $tv1_sec + 1;
	}

	return ($tv2_sec - $tv1_sec) * 1000000 + ($tv2_usec - $tv1_usec);
}

while (<>) {
	chomp;
	m/^\[(.*?)\.(.*?)\]/ || next;
	printf "%d\t| %s\n", tvdiff($tv_sec, $tv_usec, $1, $2), $_;
#	if (tvdiff($tv_sec, $tv_usec, $1, $2) > 500) {
#		printf "%d\t| %s\n", tvdiff($tv_sec, $tv_usec, $1, $2), $_;
#	}
	$tv_sec = $1;
	$tv_usec = $2;
}

