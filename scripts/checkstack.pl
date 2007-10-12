#!/usr/bin/perl

#	Check the stack usage of functions
#
#	Copyright Joern Engel <joern@wh.fh-wedel.de>
#	Inspired by Linus Torvalds
#	Original idea maybe from Keith Owens
#	s390 port and big speedup by Arnd Bergmann <arnd@bergmann-dalldorf.de>
#	Mips port by Juan Quintela <quintela@mandrakesoft.com>
#	IA64 port via Andreas Dilger
#	Arm port by Holger Schurig
#	sh64 port by Paul Mundt
#	Random bits by Matt Mackall <mpm@selenic.com>
#	M68k port by Geert Uytterhoeven and Andreas Schwab
#	AVR32 port by Haavard Skinnemoen <hskinnemoen@atmel.com>
#
#	Usage:
#	objdump -d vmlinux | stackcheck.pl [arch]
#
#	TODO :	Port to all architectures (one regex per arch)

# check for arch
#
# $re is used for two matches:
# $& (whole re) matches the complete objdump line with the stack growth
# $1 (first bracket) matches the size of the stack growth
#
# use anything else and feel the pain ;)
my (@stack, $re, $x, $xs);
{
	my $arch = shift;
	if ($arch eq "") {
		$arch = `uname -m`;
	}

	$x	= "[0-9a-f]";	# hex character
	$xs	= "[0-9a-f ]";	# hex character or space
	if ($arch eq 'arm') {
		#c0008ffc:	e24dd064	sub	sp, sp, #100	; 0x64
		$re = qr/.*sub.*sp, sp, #(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch eq 'avr32') {
		#8000008a:       20 1d           sub sp,4
		#80000ca8:       fa cd 05 b0     sub sp,sp,1456
		$re = qr/^.*sub.*sp.*,([0-9]{1,8})/o;
	} elsif ($arch =~ /^i[3456]86$/) {
		#c0105234:       81 ec ac 05 00 00       sub    $0x5ac,%esp
		$re = qr/^.*[as][du][db]    \$(0x$x{1,8}),\%esp$/o;
	} elsif ($arch eq 'x86_64') {
		#    2f60:	48 81 ec e8 05 00 00 	sub    $0x5e8,%rsp
		$re = qr/^.*[as][du][db]    \$(0x$x{1,8}),\%rsp$/o;
	} elsif ($arch eq 'ia64') {
		#e0000000044011fc:       01 0f fc 8c     adds r12=-384,r12
		$re = qr/.*adds.*r12=-(([0-9]{2}|[3-9])[0-9]{2}),r12/o;
	} elsif ($arch eq 'm68k') {
		#    2b6c:       4e56 fb70       linkw %fp,#-1168
		#  1df770:       defc ffe4       addaw #-28,%sp
		$re = qr/.*(?:linkw %fp,|addaw )#-([0-9]{1,4})(?:,%sp)?$/o;
	} elsif ($arch eq 'mips64') {
		#8800402c:       67bdfff0        daddiu  sp,sp,-16
		$re = qr/.*daddiu.*sp,sp,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch eq 'mips') {
		#88003254:       27bdffe0        addiu   sp,sp,-32
		$re = qr/.*addiu.*sp,sp,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch eq 'ppc') {
		#c00029f4:       94 21 ff 30     stwu    r1,-208(r1)
		$re = qr/.*stwu.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch eq 'ppc64') {
		#XXX
		$re = qr/.*stdu.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch eq 'powerpc') {
		$re = qr/.*st[dw]u.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch =~ /^s390x?$/) {
		#   11160:       a7 fb ff 60             aghi   %r15,-160
		$re = qr/.*ag?hi.*\%r15,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch =~ /^sh64$/) {
		#XXX: we only check for the immediate case presently,
		#     though we will want to check for the movi/sub
		#     pair for larger users. -- PFM.
		#a00048e0:       d4fc40f0        addi.l  r15,-240,r15
		$re = qr/.*addi\.l.*r15,-(([0-9]{2}|[3-9])[0-9]{2}),r15/o;
	} else {
		print("wrong or unknown architecture\n");
		exit
	}
}

sub bysize($) {
	my ($asize, $bsize);
	($asize = $a) =~ s/.*:	*(.*)$/$1/;
	($bsize = $b) =~ s/.*:	*(.*)$/$1/;
	$bsize <=> $asize
}

#
# main()
#
my $funcre = qr/^$x* <(.*)>:$/;
my $func;
my $file, $lastslash;

while (my $line = <STDIN>) {
	if ($line =~ m/$funcre/) {
		$func = $1;
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

		next if $line !~ m/^($xs*)/;
		my $addr = $1;
		$addr =~ s/ /0/g;
		$addr = "0x$addr";

		my $intro = "$addr $func [$file]:";
		my $padlen = 56 - length($intro);
		while ($padlen > 0) {
			$intro .= '	';
			$padlen -= 8;
		}
		next if ($size < 100);
		push @stack, "$intro$size\n";
	}
}

print sort bysize @stack;
