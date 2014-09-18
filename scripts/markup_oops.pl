#!/usr/bin/perl

use File::Basename;
use Math::BigInt;
use Getopt::Long;

# Copyright 2008, Intel Corporation
#
# This file is part of the Linux kernel
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# Authors:
# 	Arjan van de Ven <arjan@linux.intel.com>


my $cross_compile = "";
my $vmlinux_name = "";
my $modulefile = "";

# Get options
Getopt::Long::GetOptions(
	'cross-compile|c=s'	=> \$cross_compile,
	'module|m=s'		=> \$modulefile,
	'help|h'		=> \&usage,
) || usage ();
my $vmlinux_name = $ARGV[0];
if (!defined($vmlinux_name)) {
	my $kerver = `uname -r`;
	chomp($kerver);
	$vmlinux_name = "/lib/modules/$kerver/build/vmlinux";
	print "No vmlinux specified, assuming $vmlinux_name\n";
}
my $filename = $vmlinux_name;

# Parse the oops to find the EIP value

my $target = "0";
my $function;
my $module = "";
my $func_offset = 0;
my $vmaoffset = 0;

my %regs;


sub parse_x86_regs
{
	my ($line) = @_;
	if ($line =~ /EAX: ([0-9a-f]+) EBX: ([0-9a-f]+) ECX: ([0-9a-f]+) EDX: ([0-9a-f]+)/) {
		$regs{"%eax"} = $1;
		$regs{"%ebx"} = $2;
		$regs{"%ecx"} = $3;
		$regs{"%edx"} = $4;
	}
	if ($line =~ /ESI: ([0-9a-f]+) EDI: ([0-9a-f]+) EBP: ([0-9a-f]+) ESP: ([0-9a-f]+)/) {
		$regs{"%esi"} = $1;
		$regs{"%edi"} = $2;
		$regs{"%esp"} = $4;
	}
	if ($line =~ /RAX: ([0-9a-f]+) RBX: ([0-9a-f]+) RCX: ([0-9a-f]+)/) {
		$regs{"%eax"} = $1;
		$regs{"%ebx"} = $2;
		$regs{"%ecx"} = $3;
	}
	if ($line =~ /RDX: ([0-9a-f]+) RSI: ([0-9a-f]+) RDI: ([0-9a-f]+)/) {
		$regs{"%edx"} = $1;
		$regs{"%esi"} = $2;
		$regs{"%edi"} = $3;
	}
	if ($line =~ /RBP: ([0-9a-f]+) R08: ([0-9a-f]+) R09: ([0-9a-f]+)/) {
		$regs{"%r08"} = $2;
		$regs{"%r09"} = $3;
	}
	if ($line =~ /R10: ([0-9a-f]+) R11: ([0-9a-f]+) R12: ([0-9a-f]+)/) {
		$regs{"%r10"} = $1;
		$regs{"%r11"} = $2;
		$regs{"%r12"} = $3;
	}
	if ($line =~ /R13: ([0-9a-f]+) R14: ([0-9a-f]+) R15: ([0-9a-f]+)/) {
		$regs{"%r13"} = $1;
		$regs{"%r14"} = $2;
		$regs{"%r15"} = $3;
	}
}

sub reg_name
{
	my ($reg) = @_;
	$reg =~ s/r(.)x/e\1x/;
	$reg =~ s/r(.)i/e\1i/;
	$reg =~ s/r(.)p/e\1p/;
	return $reg;
}

sub process_x86_regs
{
	my ($line, $cntr) = @_;
	my $str = "";
	if (length($line) < 40) {
		return ""; # not an asm istruction
	}

	# find the arguments to the instruction
	if ($line =~ /([0-9a-zA-Z\,\%\(\)\-\+]+)$/) {
		$lastword = $1;
	} else {
		return "";
	}

	# we need to find the registers that get clobbered,
	# since their value is no longer relevant for previous
	# instructions in the stream.

	$clobber = $lastword;
	# first, remove all memory operands, they're read only
	$clobber =~ s/\([a-z0-9\%\,]+\)//g;
	# then, remove everything before the comma, thats the read part
	$clobber =~ s/.*\,//g;

	# if this is the instruction that faulted, we haven't actually done
	# the write yet... nothing is clobbered.
	if ($cntr == 0) {
		$clobber = "";
	}

	foreach $reg (keys(%regs)) {
		my $clobberprime = reg_name($clobber);
		my $lastwordprime = reg_name($lastword);
		my $val = $regs{$reg};
		if ($val =~ /^[0]+$/) {
			$val = "0";
		} else {
			$val =~ s/^0*//;
		}

		# first check if we're clobbering this register; if we do
		# we print it with a =>, and then delete its value
		if ($clobber =~ /$reg/ || $clobberprime =~ /$reg/) {
			if (length($val) > 0) {
				$str = $str . " $reg => $val ";
			}
			$regs{$reg} = "";
			$val = "";
		}
		# now check if we're reading this register
		if ($lastword =~ /$reg/ || $lastwordprime =~ /$reg/) {
			if (length($val) > 0) {
				$str = $str . " $reg = $val ";
			}
		}
	}
	return $str;
}

# parse the oops
while (<STDIN>) {
	my $line = $_;
	if ($line =~ /EIP: 0060:\[\<([a-z0-9]+)\>\]/) {
		$target = $1;
	}
	if ($line =~ /RIP: 0010:\[\<([a-z0-9]+)\>\]/) {
		$target = $1;
	}
	if ($line =~ /EIP is at ([a-zA-Z0-9\_]+)\+0x([0-9a-f]+)\/0x[a-f0-9]/) {
		$function = $1;
		$func_offset = $2;
	}
	if ($line =~ /RIP: 0010:\[\<[0-9a-f]+\>\]  \[\<[0-9a-f]+\>\] ([a-zA-Z0-9\_]+)\+0x([0-9a-f]+)\/0x[a-f0-9]/) {
		$function = $1;
		$func_offset = $2;
	}

	# check if it's a module
	if ($line =~ /EIP is at ([a-zA-Z0-9\_]+)\+(0x[0-9a-f]+)\/0x[a-f0-9]+\W\[([a-zA-Z0-9\_\-]+)\]/) {
		$module = $3;
	}
	if ($line =~ /RIP: 0010:\[\<[0-9a-f]+\>\]  \[\<[0-9a-f]+\>\] ([a-zA-Z0-9\_]+)\+(0x[0-9a-f]+)\/0x[a-f0-9]+\W\[([a-zA-Z0-9\_\-]+)\]/) {
		$module = $3;
	}
	parse_x86_regs($line);
}

my $decodestart = Math::BigInt->from_hex("0x$target") - Math::BigInt->from_hex("0x$func_offset");
my $decodestop = Math::BigInt->from_hex("0x$target") + 8192;
if ($target eq "0") {
	print "No oops found!\n";
	usage();
}

# if it's a module, we need to find the .ko file and calculate a load offset
if ($module ne "") {
	if ($modulefile eq "") {
		$modulefile = `modinfo -F filename $module`;
		chomp($modulefile);
	}
	$filename = $modulefile;
	if ($filename eq "") {
		print "Module .ko file for $module not found. Aborting\n";
		exit;
	}
	# ok so we found the module, now we need to calculate the vma offset
	open(FILE, $cross_compile."objdump -dS $filename |") || die "Cannot start objdump";
	while (<FILE>) {
		if ($_ =~ /^([0-9a-f]+) \<$function\>\:/) {
			my $fu = $1;
			$vmaoffset = Math::BigInt->from_hex("0x$target") - Math::BigInt->from_hex("0x$fu") - Math::BigInt->from_hex("0x$func_offset");
		}
	}
	close(FILE);
}

my $counter = 0;
my $state   = 0;
my $center  = -1;
my @lines;
my @reglines;

sub InRange {
	my ($address, $target) = @_;
	my $ad = "0x".$address;
	my $ta = "0x".$target;
	my $delta = Math::BigInt->from_hex($ad) - Math::BigInt->from_hex($ta);

	if (($delta > -4096) && ($delta < 4096)) {
		return 1;
	}
	return 0;
}



# first, parse the input into the lines array, but to keep size down,
# we only do this for 4Kb around the sweet spot

open(FILE, $cross_compile."objdump -dS --adjust-vma=$vmaoffset --start-address=$decodestart --stop-address=$decodestop $filename |") || die "Cannot start objdump";

while (<FILE>) {
	my $line = $_;
	chomp($line);
	if ($state == 0) {
		if ($line =~ /^([a-f0-9]+)\:/) {
			if (InRange($1, $target)) {
				$state = 1;
			}
		}
	}
	if ($state == 1) {
		if ($line =~ /^([a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9]+)\:/) {
			my $val = $1;
			if (!InRange($val, $target)) {
				last;
			}
			if ($val eq $target) {
				$center = $counter;
			}
		}
		$lines[$counter] = $line;

		$counter = $counter + 1;
	}
}

close(FILE);

if ($counter == 0) {
	print "No matching code found \n";
	exit;
}

if ($center == -1) {
	print "No matching code found \n";
	exit;
}

my $start;
my $finish;
my $codelines = 0;
my $binarylines = 0;
# now we go up and down in the array to find how much we want to print

$start = $center;

while ($start > 1) {
	$start = $start - 1;
	my $line = $lines[$start];
	if ($line =~ /^([a-f0-9]+)\:/) {
		$binarylines = $binarylines + 1;
	} else {
		$codelines = $codelines + 1;
	}
	if ($codelines > 10) {
		last;
	}
	if ($binarylines > 20) {
		last;
	}
}


$finish = $center;
$codelines = 0;
$binarylines = 0;
while ($finish < $counter) {
	$finish = $finish + 1;
	my $line = $lines[$finish];
	if ($line =~ /^([a-f0-9]+)\:/) {
		$binarylines = $binarylines + 1;
	} else {
		$codelines = $codelines + 1;
	}
	if ($codelines > 10) {
		last;
	}
	if ($binarylines > 20) {
		last;
	}
}


my $i;


# start annotating the registers in the asm.
# this goes from the oopsing point back, so that the annotator
# can track (opportunistically) which registers got written and
# whos value no longer is relevant.

$i = $center;
while ($i >= $start) {
	$reglines[$i] = process_x86_regs($lines[$i], $center - $i);
	$i = $i - 1;
}

$i = $start;
while ($i < $finish) {
	my $line;
	if ($i == $center) {
		$line =  "*$lines[$i] ";
	} else {
		$line =  " $lines[$i] ";
	}
	print $line;
	if (defined($reglines[$i]) && length($reglines[$i]) > 0) {
		my $c = 60 - length($line);
		while ($c > 0) { print " "; $c = $c - 1; };
		print "| $reglines[$i]";
	}
	if ($i == $center) {
		print "<--- faulting instruction";
	}
	print "\n";
	$i = $i +1;
}

sub usage {
	print <<EOT;
Usage:
  dmesg | perl $0 [OPTION] [VMLINUX]

OPTION:
  -c, --cross-compile CROSS_COMPILE	Specify the prefix used for toolchain.
  -m, --module MODULE_DIRNAME		Specify the module filename.
  -h, --help				Help.
EOT
	exit;
}
