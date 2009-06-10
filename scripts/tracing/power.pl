#!/usr/bin/perl

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
# This script turns a cstate ftrace output into a SVG graphic that shows
# historic C-state information
#
#
# 	cat /sys/kernel/debug/tracing/trace | perl power.pl > out.svg
#

my @styles;
my $base = 0;

my @pstate_last;
my @pstate_level;

$styles[0] = "fill:rgb(0,0,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[1] = "fill:rgb(0,255,0);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[2] = "fill:rgb(255,0,20);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[3] = "fill:rgb(255,255,20);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[4] = "fill:rgb(255,0,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[5] = "fill:rgb(0,255,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[6] = "fill:rgb(0,128,255);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[7] = "fill:rgb(0,255,128);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";
$styles[8] = "fill:rgb(0,25,20);fill-opacity:0.5;stroke-width:1;stroke:rgb(0,0,0)";


print "<?xml version=\"1.0\" standalone=\"no\"?> \n";
print "<svg width=\"10000\" height=\"100%\" version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n";

my $scale = 30000.0;
while (<>) {
	my $line = $_;
	if ($line =~ /([0-9\.]+)\] CSTATE: Going to C([0-9]) on cpu ([0-9]+) for ([0-9\.]+)/) {
		if ($base == 0) {
			$base = $1;
		}
		my $time = $1 - $base;
		$time = $time * $scale;
		my $C = $2;
		my $cpu = $3;
		my $y = 400 * $cpu;
		my $duration = $4 * $scale;
		my $msec = int($4 * 100000)/100.0;
		my $height = $C * 20;
		$style = $styles[$C];

		$y = $y + 140 - $height;

		$x2 = $time + 4;
		$y2 = $y + 4;


		print "<rect x=\"$time\" width=\"$duration\" y=\"$y\" height=\"$height\" style=\"$style\"/>\n";
		print "<text transform=\"translate($x2,$y2) rotate(90)\">C$C $msec</text>\n";
	}
	if ($line =~ /([0-9\.]+)\] PSTATE: Going to P([0-9]) on cpu ([0-9]+)/) {
		my $time = $1 - $base;
		my $state = $2;
		my $cpu = $3;

		if (defined($pstate_last[$cpu])) {
			my $from = $pstate_last[$cpu];
			my $oldstate = $pstate_state[$cpu];
			my $duration = ($time-$from) * $scale;

			$from = $from * $scale;
			my $to = $from + $duration;
			my $height = 140 - ($oldstate * (140/8));

			my $y = 400 * $cpu + 200 + $height;
			my $y2 = $y+4;
			my $style = $styles[8];

			print "<rect x=\"$from\" y=\"$y\" width=\"$duration\" height=\"5\" style=\"$style\"/>\n";
			print "<text transform=\"translate($from,$y2)\">P$oldstate (cpu $cpu)</text>\n";
		};

		$pstate_last[$cpu] = $time;
		$pstate_state[$cpu] = $state;
	}
}


print "</svg>\n";
