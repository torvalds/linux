#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# March 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that it
# uses 256 bytes per-key table [+128 bytes shared table]. Performance
# results are for streamed GHASH subroutine on UltraSPARC pre-Tx CPU
# and are expressed in cycles per processed byte, less is better:
#
#		gcc 3.3.x	cc 5.2		this assembler
#
# 32-bit build	81.4		43.3		12.6	(+546%/+244%)
# 64-bit build	20.2		21.2		12.6	(+60%/+68%)
#
# Here is data collected on UltraSPARC T1 system running Linux:
#
#		gcc 4.4.1			this assembler
#
# 32-bit build	566				50	(+1000%)
# 64-bit build	56				50	(+12%)
#
# I don't quite understand why difference between 32-bit and 64-bit
# compiler-generated code is so big. Compilers *were* instructed to
# generate code for UltraSPARC and should have used 64-bit registers
# for Z vector (see C code) even in 32-bit build... Oh well, it only
# means more impressive improvement coefficients for this assembler
# module;-) Loops are aggressively modulo-scheduled in respect to
# references to input data and Z.hi updates to achieve 12 cycles
# timing. To anchor to something else, sha1-sparcv9.pl spends 11.6
# cycles to process one byte on UltraSPARC pre-Tx CPU and ~24 on T1.

$bits=32;
for (@ARGV)     { $bits=64 if (/\-m64/ || /\-xarch\=v9/); }
if ($bits==64)  { $bias=2047; $frame=192; }
else            { $bias=0;    $frame=112; }

$output=shift;
open STDOUT,">$output";

$Zhi="%o0";	# 64-bit values
$Zlo="%o1";
$Thi="%o2";
$Tlo="%o3";
$rem="%o4";
$tmp="%o5";

$nhi="%l0";	# small values and pointers
$nlo="%l1";
$xi0="%l2";
$xi1="%l3";
$rem_4bit="%l4";
$remi="%l5";
$Htblo="%l6";
$cnt="%l7";

$Xi="%i0";	# input argument block
$Htbl="%i1";
$inp="%i2";
$len="%i3";

$code.=<<___;
.section	".rodata",#alloc

.align	64
rem_4bit:
	.long	`0x0000<<16`,0,`0x1C20<<16`,0,`0x3840<<16`,0,`0x2460<<16`,0
	.long	`0x7080<<16`,0,`0x6CA0<<16`,0,`0x48C0<<16`,0,`0x54E0<<16`,0
	.long	`0xE100<<16`,0,`0xFD20<<16`,0,`0xD940<<16`,0,`0xC560<<16`,0
	.long	`0x9180<<16`,0,`0x8DA0<<16`,0,`0xA9C0<<16`,0,`0xB5E0<<16`,0
.type	rem_4bit,#object
.size	rem_4bit,(.-rem_4bit)

.section	".text",#alloc,#execinstr
.globl	gcm_ghash_4bit
.align	32
gcm_ghash_4bit:
	save	%sp,-$frame,%sp
#ifdef __PIC__
	sethi	%hi(_GLOBAL_OFFSET_TABLE_-4), $tmp
	rd	%pc, $rem
	or	$tmp, %lo(_GLOBAL_OFFSET_TABLE_+4), $tmp
	add	$tmp, $rem, $tmp
#endif

	ldub	[$inp+15],$nlo
	ldub	[$Xi+15],$xi0
	ldub	[$Xi+14],$xi1
	add	$len,$inp,$len
	add	$Htbl,8,$Htblo

#ifdef __PIC__
	set	rem_4bit, $rem_4bit
	ldx	[$rem_4bit+$tmp], $rem_4bit
#else
	set	rem_4bit, $rem_4bit
#endif

.Louter:
	xor	$xi0,$nlo,$nlo
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	sll	$nlo,4,$nlo
	ldx	[$Htblo+$nlo],$Zlo
	ldx	[$Htbl+$nlo],$Zhi

	ldub	[$inp+14],$nlo

	ldx	[$Htblo+$nhi],$Tlo
	and	$Zlo,0xf,$remi
	ldx	[$Htbl+$nhi],$Thi
	sll	$remi,3,$remi
	ldx	[$rem_4bit+$remi],$rem
	srlx	$Zlo,4,$Zlo
	mov	13,$cnt
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo

	xor	$xi1,$nlo,$nlo
	and	$Zlo,0xf,$remi
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	ba	.Lghash_inner
	sll	$nlo,4,$nlo
.align	32
.Lghash_inner:
	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$inp+$cnt],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	ldub	[$Xi+$cnt],$xi1
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$xi1,$nlo,$nlo
	srlx	$Zhi,4,$Zhi
	and	$nlo,0xf0,$nhi
	addcc	$cnt,-1,$cnt
	xor	$Zlo,$tmp,$Zlo
	and	$nlo,0x0f,$nlo
	xor	$Tlo,$Zlo,$Zlo
	sll	$nlo,4,$nlo
	blu	.Lghash_inner
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi

	add	$inp,16,$inp
	cmp	$inp,$len
	be,pn	`$bits==64?"%xcc":"%icc"`,.Ldone
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$inp+15],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]
	srl	$Zlo,8,$xi1
	and	$Zlo,0xff,$xi0
	ba	.Louter
	and	$xi1,0xff,$xi1
.align	32
.Ldone:
	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]

	ret
	restore
.type	gcm_ghash_4bit,#function
.size	gcm_ghash_4bit,(.-gcm_ghash_4bit)
___

undef $inp;
undef $len;

$code.=<<___;
.globl	gcm_gmult_4bit
.align	32
gcm_gmult_4bit:
	save	%sp,-$frame,%sp
#ifdef __PIC__
	sethi	%hi(_GLOBAL_OFFSET_TABLE_-4), $tmp
	rd	%pc, $rem
	or	$tmp, %lo(_GLOBAL_OFFSET_TABLE_+4), $tmp
	add	$tmp, $rem, $tmp
#endif

	ldub	[$Xi+15],$nlo
	add	$Htbl,8,$Htblo

#ifdef __PIC__
	set	rem_4bit, $rem_4bit
	ldx	[$rem_4bit+$tmp], $rem_4bit
#else
	set	rem_4bit, $rem_4bit
#endif

	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	sll	$nlo,4,$nlo
	ldx	[$Htblo+$nlo],$Zlo
	ldx	[$Htbl+$nlo],$Zhi

	ldub	[$Xi+14],$nlo

	ldx	[$Htblo+$nhi],$Tlo
	and	$Zlo,0xf,$remi
	ldx	[$Htbl+$nhi],$Thi
	sll	$remi,3,$remi
	ldx	[$rem_4bit+$remi],$rem
	srlx	$Zlo,4,$Zlo
	mov	13,$cnt
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo

	and	$Zlo,0xf,$remi
	and	$nlo,0xf0,$nhi
	and	$nlo,0x0f,$nlo
	ba	.Lgmult_inner
	sll	$nlo,4,$nlo
.align	32
.Lgmult_inner:
	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	ldub	[$Xi+$cnt],$nlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	srlx	$Zhi,4,$Zhi
	and	$nlo,0xf0,$nhi
	addcc	$cnt,-1,$cnt
	xor	$Zlo,$tmp,$Zlo
	and	$nlo,0x0f,$nlo
	xor	$Tlo,$Zlo,$Zlo
	sll	$nlo,4,$nlo
	blu	.Lgmult_inner
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nlo],$Tlo
	sll	$remi,3,$remi
	xor	$Thi,$Zhi,$Zhi
	ldx	[$Htbl+$nlo],$Thi
	srlx	$Zlo,4,$Zlo
	xor	$rem,$Zhi,$Zhi
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	and	$Zlo,0xf,$remi

	ldx	[$Htblo+$nhi],$Tlo
	sll	$remi,3,$remi
	xor	$rem,$Zhi,$Zhi
	ldx	[$Htbl+$nhi],$Thi
	srlx	$Zlo,4,$Zlo
	ldx	[$rem_4bit+$remi],$rem
	sllx	$Zhi,60,$tmp
	xor	$Tlo,$Zlo,$Zlo
	srlx	$Zhi,4,$Zhi
	xor	$Zlo,$tmp,$Zlo
	xor	$Thi,$Zhi,$Zhi
	stx	$Zlo,[$Xi+8]
	xor	$rem,$Zhi,$Zhi
	stx	$Zhi,[$Xi]

	ret
	restore
.type	gcm_gmult_4bit,#function
.size	gcm_gmult_4bit,(.-gcm_gmult_4bit)
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;
