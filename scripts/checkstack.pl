#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

#	Check the stack usage of functions
#
#	Copyright Joern Engel <joern@lazybastard.org>
#	Inspired by Linus Torvalds
#	Original idea maybe from Keith Owens
#	s390 port and big speedup by Arnd Bergmann <arnd@bergmann-dalldorf.de>
#	Mips port by Juan Quintela <quintela@mandrakesoft.com>
#	Arm port by Holger Schurig
#	Random bits by Matt Mackall <mpm@selenic.com>
#	M68k port by Geert Uytterhoeven and Andreas Schwab
#	AArch64, PARISC ports by Kyle McMartin
#	sparc port by Martin Habets <errandir_news@mph.eclipse.co.uk>
#	ppc64le port by Breno Leitao <leitao@debian.org>
#	riscv port by Wadim Mueller <wafgo01@gmail.com>
#	loongarch port by Youling Tang <tangyouling@kylinos.cn>
#
#	Usage:
#	objdump -d vmlinux | scripts/checkstack.pl [arch] [min_stack]
#
#	TODO :	Port to all architectures (one regex per arch)

use strict;

# check for arch
#
# $re is used for two matches:
# $& (whole re) matches the complete objdump line with the stack growth
# $1 (first bracket) matches the size of the stack growth
#
# $dre is similar, but for dynamic stack redutions:
# $& (whole re) matches the complete objdump line with the stack growth
# $1 (first bracket) matches the dynamic amount of the stack growth
#
# $sub: subroutine for special handling to check stack usage.
#
# use anything else and feel the pain ;)
my (@stack, $re, $dre, $sub, $x, $xs, $funcre, $min_stack);
{
	my $arch = shift;
	if ($arch eq "") {
		$arch = `uname -m`;
		chomp($arch);
	}

	$min_stack = shift;
	if ($min_stack eq "" || $min_stack !~ /^\d+$/) {
		$min_stack = 512;
	}

	$x	= "[0-9a-f]";	# hex character
	$xs	= "[0-9a-f ]";	# hex character or space
	$funcre = qr/^$x* <(.*)>:$/;
	if ($arch =~ '^(aarch|arm)64$') {
		#ffffffc0006325cc:       a9bb7bfd        stp     x29, x30, [sp, #-80]!
		#a110:       d11643ff        sub     sp, sp, #0x590
		$re = qr/^.*stp.*sp, ?\#-([0-9]{1,8})\]\!/o;
		$dre = qr/^.*sub.*sp, sp, #(0x$x{1,8})/o;
	} elsif ($arch eq 'arm') {
		#c0008ffc:	e24dd064	sub	sp, sp, #100	; 0x64
		$re = qr/.*sub.*sp, sp, #([0-9]{1,4})/o;
		$sub = \&arm_push_handling;
	} elsif ($arch =~ /^x86(_64)?$/ || $arch =~ /^i[3456]86$/) {
		#c0105234:       81 ec ac 05 00 00       sub    $0x5ac,%esp
		# or
		#    2f60:    48 81 ec e8 05 00 00       sub    $0x5e8,%rsp
		$re = qr/^.*[as][du][db]    \$(0x$x{1,8}),\%(e|r)sp$/o;
		$dre = qr/^.*[as][du][db]    (%.*),\%(e|r)sp$/o;
	} elsif ($arch eq 'm68k') {
		#    2b6c:       4e56 fb70       linkw %fp,#-1168
		#  1df770:       defc ffe4       addaw #-28,%sp
		$re = qr/.*(?:linkw %fp,|addaw )#-([0-9]{1,4})(?:,%sp)?$/o;
	} elsif ($arch eq 'mips64') {
		#8800402c:       67bdfff0        daddiu  sp,sp,-16
		$re = qr/.*daddiu.*sp,sp,-([0-9]{1,8})/o;
	} elsif ($arch eq 'mips') {
		#88003254:       27bdffe0        addiu   sp,sp,-32
		$re = qr/.*addiu.*sp,sp,-([0-9]{1,8})/o;
	} elsif ($arch eq 'nios2') {
		#25a8:	defffb04 	addi	sp,sp,-20
		$re = qr/.*addi.*sp,sp,-([0-9]{1,8})/o;
	} elsif ($arch eq 'openrisc') {
		# c000043c:       9c 21 fe f0     l.addi r1,r1,-272
		$re = qr/.*l\.addi.*r1,r1,-([0-9]{1,8})/o;
	} elsif ($arch eq 'parisc' || $arch eq 'parisc64') {
		$re = qr/.*ldo ($x{1,8})\(sp\),sp/o;
	} elsif ($arch eq 'powerpc' || $arch =~ /^ppc(64)?(le)?$/ ) {
		# powerpc    : 94 21 ff 30     stwu    r1,-208(r1)
		# ppc64(le)  : 81 ff 21 f8     stdu    r1,-128(r1)
		$re = qr/.*st[dw]u.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch =~ /^s390x?$/) {
		#   11160:       a7 fb ff 60             aghi   %r15,-160
		# or
		#  100092:	 e3 f0 ff c8 ff 71	 lay	 %r15,-56(%r15)
		$re = qr/.*(?:lay|ag?hi).*\%r15,-([0-9]+)(?:\(\%r15\))?$/o;
	} elsif ($arch eq 'sparc' || $arch eq 'sparc64') {
		# f0019d10:       9d e3 bf 90     save  %sp, -112, %sp
		$re = qr/.*save.*%sp, -([0-9]{1,8}), %sp/o;
	} elsif ($arch =~ /^riscv(64)?$/) {
		#ffffffff8036e868:	c2010113          	addi	sp,sp,-992
		$re = qr/.*addi.*sp,sp,-([0-9]{1,8})/o;
	} elsif ($arch =~ /^loongarch(32|64)?$/) {
		#9000000000224708:	02ff4063		addi.d  $sp, $sp, -48(0xfd0)
		$re = qr/.*addi\..*sp, .*sp, -([0-9]{1,8}).*/o;
	} else {
		print("wrong or unknown architecture \"$arch\"\n");
		exit
	}
}

#
# To count stack usage of push {*, fp, ip, lr, pc} instruction in ARM,
# if FRAME POINTER is enabled.
# e.g. c01f0d48: e92ddff0 push {r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, pc}
#
sub arm_push_handling {
	my $regex = qr/.*push.*fp, ip, lr, pc}/o;
	my $size = 0;
	my $line_arg = shift;

	if ($line_arg =~ m/$regex/) {
		$size = $line_arg =~ tr/,//;
		$size = ($size + 1) * 4;
	}

	return $size;
}

#
# main()
#
my ($func, $file, $lastslash, $total_size, $addr, $intro);

$total_size = 0;

while (my $line = <STDIN>) {
	if ($line =~ m/$funcre/) {
		$func = $1;
		next if $line !~ m/^($x*)/;
		if ($total_size > $min_stack) {
			push @stack, "$intro$total_size\n";
		}
		$addr = "0x$1";
		$intro = "$addr $func [$file]:";
		my $padlen = 56 - length($intro);
		while ($padlen > 0) {
			$intro .= '	';
			$padlen -= 8;
		}

		$total_size = 0;
	}
	elsif ($line =~ m/(.*):\s*file format/) {
		$file = $1;
		$file =~ s/\.ko//;
		$lastslash = rindex($file, "/");
		if ($lastslash != -1) {
			$file = substr($file, $lastslash + 1);
		}
	}
	elsif ($line =~ m/$re/) {
		my $size = $1;
		$size = hex($size) if ($size =~ /^0x/);

		if ($size > 0xf0000000) {
			$size = - $size;
			$size += 0x80000000;
			$size += 0x80000000;
		}
		next if ($size > 0x10000000);

		$total_size += $size;
	}
	elsif (defined $dre && $line =~ m/$dre/) {
		my $size = $1;

		$size = hex($size) if ($size =~ /^0x/);
		$total_size += $size;
	}
	elsif (defined $sub) {
		my $size = &$sub($line);

		$total_size += $size;
	}
}
if ($total_size > $min_stack) {
	push @stack, "$intro$total_size\n";
}

# Sort output by size (last field) and function name if size is the same
sub sort_lines {
	my ($a, $b) = @_;

	my $num_a = $1 if $a =~ /:\t*(\d+)$/;
	my $num_b = $1 if $b =~ /:\t*(\d+)$/;
	my $func_a = $1 if $a =~ / (.*):/;
	my $func_b = $1 if $b =~ / (.*):/;

	if ($num_a != $num_b) {
		return $num_b <=> $num_a;
	} else {
		return $func_a cmp $func_b;
	}
}

print sort { sort_lines($a, $b) } @stack;
