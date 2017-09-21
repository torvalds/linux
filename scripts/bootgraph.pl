#!/usr/bin/env perl

# Copyright 2008, Intel Corporation
#
# This file is part of the Linux kernel
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#
# Authors:
# 	Arjan van de Ven <arjan@linux.intel.com>


#
# This script turns a dmesg output into a SVG graphic that shows which
# functions take how much time. You can view SVG graphics with various
# programs, including Inkscape, The Gimp and Firefox.
#
#
# For this script to work, the kernel needs to be compiled with the
# CONFIG_PRINTK_TIME configuration option enabled, and with
# "initcall_debug" passed on the kernel command line.
#
# usage:
# 	dmesg | perl scripts/bootgraph.pl > output.svg
#

use strict;
use Getopt::Long;
my $header = 0;

sub help {
	my $text = << "EOM";
Usage:
1) dmesg | perl scripts/bootgraph.pl [OPTION] > output.svg
2) perl scripts/bootgraph.pl -h

Options:
	-header	Insert kernel version and date
EOM
	my $std=shift;
	if ($std == 1) {
		print STDERR $text;
	} else {
		print $text;
	}
	exit;
}

GetOptions(
	'h|help'	=>\&help,
	'header'	=>\$header
);

my %start;
my %end;
my %type;
my $done = 0;
my $maxtime = 0;
my $firsttime = 99999;
my $count = 0;
my %pids;
my %pidctr;

my $headerstep = 20;
my $xheader = 15;
my $yheader = 25;
my $cyheader = 0;

while (<>) {
	my $line = $_;
	if ($line =~ /([0-9\.]+)\] calling  ([a-zA-Z0-9\_\.]+)\+/) {
		my $func = $2;
		if ($done == 0) {
			$start{$func} = $1;
			$type{$func} = 0;
			if ($1 < $firsttime) {
				$firsttime = $1;
			}
		}
		if ($line =~ /\@ ([0-9]+)/) {
			$pids{$func} = $1;
		}
		$count = $count + 1;
	}

	if ($line =~ /([0-9\.]+)\] async_waiting @ ([0-9]+)/) {
		my $pid = $2;
		my $func;
		if (!defined($pidctr{$pid})) {
			$func = "wait_" . $pid . "_1";
			$pidctr{$pid} = 1;
		} else {
			$pidctr{$pid} = $pidctr{$pid} + 1;
			$func = "wait_" . $pid . "_" . $pidctr{$pid};
		}
		if ($done == 0) {
			$start{$func} = $1;
			$type{$func} = 1;
			if ($1 < $firsttime) {
				$firsttime = $1;
			}
		}
		$pids{$func} = $pid;
		$count = $count + 1;
	}

	if ($line =~ /([0-9\.]+)\] initcall ([a-zA-Z0-9\_\.]+)\+.*returned/) {
		if ($done == 0) {
			$end{$2} = $1;
			$maxtime = $1;
		}
	}

	if ($line =~ /([0-9\.]+)\] async_continuing @ ([0-9]+)/) {
		my $pid = $2;
		my $func =  "wait_" . $pid . "_" . $pidctr{$pid};
		$end{$func} = $1;
		$maxtime = $1;
	}
	if ($line =~ /Write protecting the/) {
		$done = 1;
	}
	if ($line =~ /Freeing unused kernel memory/) {
		$done = 1;
	}
}

if ($count == 0) {
    print STDERR <<END;
No data found in the dmesg. Make sure that 'printk.time=1' and
'initcall_debug' are passed on the kernel command line.
END
	help(1);
    exit 1;
}

print "<?xml version=\"1.0\" standalone=\"no\"?> \n";
print "<svg width=\"2000\" height=\"100%\" version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n";


if ($header) {
	my $version = `uname -a`;
	my $date = `date`;
	print "<text transform=\"translate($xheader,$yheader)\">Kernel version: $version</text>\n";
	$cyheader = $yheader+$headerstep;
	print "<text transform=\"translate($xheader,$cyheader)\">Date: $date</text>\n";
}

my @styles;

$styles[0] = "fill:rgb(0,0,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[1] = "fill:rgb(0,255,0);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[2] = "fill:rgb(255,0,20);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[3] = "fill:rgb(255,255,20);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[4] = "fill:rgb(255,0,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[5] = "fill:rgb(0,255,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[6] = "fill:rgb(0,128,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[7] = "fill:rgb(0,255,128);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[8] = "fill:rgb(255,0,128);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[9] = "fill:rgb(255,255,128);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[10] = "fill:rgb(255,128,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[11] = "fill:rgb(128,255,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";

my $style_wait = "fill:rgb(128,128,128);fill-opacity:0.5;stroke-width:0;stroke:rgb(0,0,0)";

my $mult = 1950.0 / ($maxtime - $firsttime);
my $threshold2 = ($maxtime - $firsttime) / 120.0;
my $threshold = $threshold2/10;
my $stylecounter = 0;
my %rows;
my $rowscount = 1;
my @initcalls = sort { $start{$a} <=> $start{$b} } keys(%start);

foreach my $key (@initcalls) {
	my $duration = $end{$key} - $start{$key};

	if ($duration >= $threshold) {
		my ($s, $s2, $s3, $e, $w, $y, $y2, $style);
		my $pid = $pids{$key};

		if (!defined($rows{$pid})) {
			$rows{$pid} = $rowscount;
			$rowscount = $rowscount + 1;
		}
		$s = ($start{$key} - $firsttime) * $mult;
		$s2 = $s + 6;
		$s3 = $s + 1;
		$e = ($end{$key} - $firsttime) * $mult;
		$w = $e - $s;

		$y = $rows{$pid} * 150;
		$y2 = $y + 4;

		$style = $styles[$stylecounter];
		$stylecounter = $stylecounter + 1;
		if ($stylecounter > 11) {
			$stylecounter = 0;
		};

		if ($type{$key} == 1) {
			$y = $y + 15;
			print "<rect x=\"$s\" width=\"$w\" y=\"$y\" height=\"115\" style=\"$style_wait\"/>\n";
		} else {
			print "<rect x=\"$s\" width=\"$w\" y=\"$y\" height=\"145\" style=\"$style\"/>\n";
			if ($duration >= $threshold2) {
				print "<text transform=\"translate($s2,$y2) rotate(90)\">$key</text>\n";
			} else {
				print "<text transform=\"translate($s3,$y2) rotate(90)\" font-size=\"3pt\">$key</text>\n";
			}
		}
	}
}


# print the time line on top
my $time = $firsttime;
my $step = ($maxtime - $firsttime) / 15;
while ($time < $maxtime) {
	my $s3 = ($time - $firsttime) * $mult;
	my $tm = int($time * 100) / 100.0;
	print "<text transform=\"translate($s3,89) rotate(90)\">$tm</text>\n";
	$time = $time + $step;
}

print "</svg>\n";
