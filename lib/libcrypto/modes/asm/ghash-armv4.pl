#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# April 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that it
# uses 256 bytes per-key table [+32 bytes shared table]. There is no
# experimental performance data available yet. The only approximation
# that can be made at this point is based on code size. Inner loop is
# 32 instructions long and on single-issue core should execute in <40
# cycles. Having verified that gcc 3.4 didn't unroll corresponding
# loop, this assembler loop body was found to be ~3x smaller than
# compiler-generated one...
#
# July 2010
#
# Rescheduling for dual-issue pipeline resulted in 8.5% improvement on
# Cortex A8 core and ~25 cycles per processed byte (which was observed
# to be ~3 times faster than gcc-generated code:-)
#
# February 2011
#
# Profiler-assisted and platform-specific optimization resulted in 7%
# improvement on Cortex A8 core and ~23.5 cycles per byte.
#
# March 2011
#
# Add NEON implementation featuring polynomial multiplication, i.e. no
# lookup tables involved. On Cortex A8 it was measured to process one
# byte in 15 cycles or 55% faster than integer-only code.

# ====================================================================
# Note about "528B" variant. In ARM case it makes lesser sense to
# implement it for following reasons:
#
# - performance improvement won't be anywhere near 50%, because 128-
#   bit shift operation is neatly fused with 128-bit xor here, and
#   "538B" variant would eliminate only 4-5 instructions out of 32
#   in the inner loop (meaning that estimated improvement is ~15%);
# - ARM-based systems are often embedded ones and extra memory
#   consumption might be unappreciated (for so little improvement);
#
# Byte order [in]dependence. =========================================
#
# Caller is expected to maintain specific *dword* order in Htable,
# namely with *least* significant dword of 128-bit value at *lower*
# address. This differs completely from C code and has everything to
# do with ldm instruction and order in which dwords are "consumed" by
# algorithm. *Byte* order within these dwords in turn is whatever
# *native* byte order on current platform. See gcm128.c for working
# example...

while (($output=shift) && ($output!~/^\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

$Xi="r0";	# argument block
$Htbl="r1";
$inp="r2";
$len="r3";

$Zll="r4";	# variables
$Zlh="r5";
$Zhl="r6";
$Zhh="r7";
$Tll="r8";
$Tlh="r9";
$Thl="r10";
$Thh="r11";
$nlo="r12";
################# r13 is stack pointer
$nhi="r14";
################# r15 is program counter

$rem_4bit=$inp;	# used in gcm_gmult_4bit
$cnt=$len;

sub Zsmash() {
  my $i=12;
  my @args=@_;
  for ($Zll,$Zlh,$Zhl,$Zhh) {
    $code.=<<___;
#if __ARM_ARCH__>=7 && defined(__ARMEL__)
	rev	$_,$_
	str	$_,[$Xi,#$i]
#elif defined(__ARMEB__)
	str	$_,[$Xi,#$i]
#else
	mov	$Tlh,$_,lsr#8
	strb	$_,[$Xi,#$i+3]
	mov	$Thl,$_,lsr#16
	strb	$Tlh,[$Xi,#$i+2]
	mov	$Thh,$_,lsr#24
	strb	$Thl,[$Xi,#$i+1]
	strb	$Thh,[$Xi,#$i]
#endif
___
    $code.="\t".shift(@args)."\n";
    $i-=4;
  }
}

$code=<<___;
#include "arm_arch.h"

.text
.syntax	unified
.code	32

.type	rem_4bit,%object
.align	5
rem_4bit:
.short	0x0000,0x1C20,0x3840,0x2460
.short	0x7080,0x6CA0,0x48C0,0x54E0
.short	0xE100,0xFD20,0xD940,0xC560
.short	0x9180,0x8DA0,0xA9C0,0xB5E0
.size	rem_4bit,.-rem_4bit

.type	rem_4bit_get,%function
rem_4bit_get:
	sub	$rem_4bit,pc,#8
	sub	$rem_4bit,$rem_4bit,#32	@ &rem_4bit
	b	.Lrem_4bit_got
	nop
.size	rem_4bit_get,.-rem_4bit_get

.global	gcm_ghash_4bit
.type	gcm_ghash_4bit,%function
gcm_ghash_4bit:
	sub	r12,pc,#8
	add	$len,$inp,$len		@ $len to point at the end
	stmdb	sp!,{r3-r11,lr}		@ save $len/end too
	sub	r12,r12,#48		@ &rem_4bit

	ldmia	r12,{r4-r11}		@ copy rem_4bit ...
	stmdb	sp!,{r4-r11}		@ ... to stack

	ldrb	$nlo,[$inp,#15]
	ldrb	$nhi,[$Xi,#15]
.Louter:
	eor	$nlo,$nlo,$nhi
	and	$nhi,$nlo,#0xf0
	and	$nlo,$nlo,#0x0f
	mov	$cnt,#14

	add	$Zhh,$Htbl,$nlo,lsl#4
	ldmia	$Zhh,{$Zll-$Zhh}	@ load Htbl[nlo]
	add	$Thh,$Htbl,$nhi
	ldrb	$nlo,[$inp,#14]

	and	$nhi,$Zll,#0xf		@ rem
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nhi]
	add	$nhi,$nhi,$nhi
	eor	$Zll,$Tll,$Zll,lsr#4
	ldrh	$Tll,[sp,$nhi]		@ rem_4bit[rem]
	eor	$Zll,$Zll,$Zlh,lsl#28
	ldrb	$nhi,[$Xi,#14]
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	eor	$Zhl,$Thl,$Zhl,lsr#4
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eor	$Zhh,$Thh,$Zhh,lsr#4
	eor	$nlo,$nlo,$nhi
	and	$nhi,$nlo,#0xf0
	and	$nlo,$nlo,#0x0f
	eor	$Zhh,$Zhh,$Tll,lsl#16

.Linner:
	add	$Thh,$Htbl,$nlo,lsl#4
	and	$nlo,$Zll,#0xf		@ rem
	subs	$cnt,$cnt,#1
	add	$nlo,$nlo,$nlo
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nlo]
	eor	$Zll,$Tll,$Zll,lsr#4
	eor	$Zll,$Zll,$Zlh,lsl#28
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	ldrh	$Tll,[sp,$nlo]		@ rem_4bit[rem]
	eor	$Zhl,$Thl,$Zhl,lsr#4
	ldrbpl	$nlo,[$inp,$cnt]
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eor	$Zhh,$Thh,$Zhh,lsr#4

	add	$Thh,$Htbl,$nhi
	and	$nhi,$Zll,#0xf		@ rem
	eor	$Zhh,$Zhh,$Tll,lsl#16	@ ^= rem_4bit[rem]
	add	$nhi,$nhi,$nhi
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nhi]
	eor	$Zll,$Tll,$Zll,lsr#4
	ldrbpl	$Tll,[$Xi,$cnt]
	eor	$Zll,$Zll,$Zlh,lsl#28
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	ldrh	$Tlh,[sp,$nhi]
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	eor	$Zhl,$Thl,$Zhl,lsr#4
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eorpl	$nlo,$nlo,$Tll
	eor	$Zhh,$Thh,$Zhh,lsr#4
	andpl	$nhi,$nlo,#0xf0
	andpl	$nlo,$nlo,#0x0f
	eor	$Zhh,$Zhh,$Tlh,lsl#16	@ ^= rem_4bit[rem]
	bpl	.Linner

	ldr	$len,[sp,#32]		@ re-load $len/end
	add	$inp,$inp,#16
	mov	$nhi,$Zll
___
	&Zsmash("cmp\t$inp,$len","ldrbne\t$nlo,[$inp,#15]");
$code.=<<___;
	bne	.Louter

	add	sp,sp,#36
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r11,pc}
#else
	ldmia	sp!,{r4-r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	gcm_ghash_4bit,.-gcm_ghash_4bit

.global	gcm_gmult_4bit
.type	gcm_gmult_4bit,%function
gcm_gmult_4bit:
	stmdb	sp!,{r4-r11,lr}
	ldrb	$nlo,[$Xi,#15]
	b	rem_4bit_get
.Lrem_4bit_got:
	and	$nhi,$nlo,#0xf0
	and	$nlo,$nlo,#0x0f
	mov	$cnt,#14

	add	$Zhh,$Htbl,$nlo,lsl#4
	ldmia	$Zhh,{$Zll-$Zhh}	@ load Htbl[nlo]
	ldrb	$nlo,[$Xi,#14]

	add	$Thh,$Htbl,$nhi
	and	$nhi,$Zll,#0xf		@ rem
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nhi]
	add	$nhi,$nhi,$nhi
	eor	$Zll,$Tll,$Zll,lsr#4
	ldrh	$Tll,[$rem_4bit,$nhi]	@ rem_4bit[rem]
	eor	$Zll,$Zll,$Zlh,lsl#28
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	eor	$Zhl,$Thl,$Zhl,lsr#4
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eor	$Zhh,$Thh,$Zhh,lsr#4
	and	$nhi,$nlo,#0xf0
	eor	$Zhh,$Zhh,$Tll,lsl#16
	and	$nlo,$nlo,#0x0f

.Loop:
	add	$Thh,$Htbl,$nlo,lsl#4
	and	$nlo,$Zll,#0xf		@ rem
	subs	$cnt,$cnt,#1
	add	$nlo,$nlo,$nlo
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nlo]
	eor	$Zll,$Tll,$Zll,lsr#4
	eor	$Zll,$Zll,$Zlh,lsl#28
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	ldrh	$Tll,[$rem_4bit,$nlo]	@ rem_4bit[rem]
	eor	$Zhl,$Thl,$Zhl,lsr#4
	ldrbpl	$nlo,[$Xi,$cnt]
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eor	$Zhh,$Thh,$Zhh,lsr#4

	add	$Thh,$Htbl,$nhi
	and	$nhi,$Zll,#0xf		@ rem
	eor	$Zhh,$Zhh,$Tll,lsl#16	@ ^= rem_4bit[rem]
	add	$nhi,$nhi,$nhi
	ldmia	$Thh,{$Tll-$Thh}	@ load Htbl[nhi]
	eor	$Zll,$Tll,$Zll,lsr#4
	eor	$Zll,$Zll,$Zlh,lsl#28
	eor	$Zlh,$Tlh,$Zlh,lsr#4
	ldrh	$Tll,[$rem_4bit,$nhi]	@ rem_4bit[rem]
	eor	$Zlh,$Zlh,$Zhl,lsl#28
	eor	$Zhl,$Thl,$Zhl,lsr#4
	eor	$Zhl,$Zhl,$Zhh,lsl#28
	eor	$Zhh,$Thh,$Zhh,lsr#4
	andpl	$nhi,$nlo,#0xf0
	andpl	$nlo,$nlo,#0x0f
	eor	$Zhh,$Zhh,$Tll,lsl#16	@ ^= rem_4bit[rem]
	bpl	.Loop
___
	&Zsmash();
$code.=<<___;
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r11,pc}
#else
	ldmia	sp!,{r4-r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	gcm_gmult_4bit,.-gcm_gmult_4bit
___
{
my $cnt=$Htbl;	# $Htbl is used once in the very beginning

my ($Hhi, $Hlo, $Zo, $T, $xi, $mod) = map("d$_",(0..7));
my ($Qhi, $Qlo, $Z,  $R, $zero, $Qpost, $IN) = map("q$_",(8..15));

# Z:Zo keeps 128-bit result shifted by 1 to the right, with bottom bit
# in Zo. Or should I say "top bit", because GHASH is specified in
# reverse bit order? Otherwise straightforward 128-bt H by one input
# byte multiplication and modulo-reduction, times 16.

sub Dlo()   { shift=~m|q([1]?[0-9])|?"d".($1*2):"";     }
sub Dhi()   { shift=~m|q([1]?[0-9])|?"d".($1*2+1):"";   }
sub Q()     { shift=~m|d([1-3]?[02468])|?"q".($1/2):""; }

$code.=<<___;
#if __ARM_ARCH__>=7 && !defined(__STRICT_ALIGNMENT)
.fpu	neon

.global	gcm_gmult_neon
.type	gcm_gmult_neon,%function
.align	4
gcm_gmult_neon:
	sub		$Htbl,#16		@ point at H in GCM128_CTX
	vld1.64		`&Dhi("$IN")`,[$Xi,:64]!@ load Xi
	vmov.i32	$mod,#0xe1		@ our irreducible polynomial
	vld1.64		`&Dlo("$IN")`,[$Xi,:64]!
	vshr.u64	$mod,#32
	vldmia		$Htbl,{$Hhi-$Hlo}	@ load H
	veor		$zero,$zero
#ifdef __ARMEL__
	vrev64.8	$IN,$IN
#endif
	veor		$Qpost,$Qpost
	veor		$R,$R
	mov		$cnt,#16
	veor		$Z,$Z
	mov		$len,#16
	veor		$Zo,$Zo
	vdup.8		$xi,`&Dlo("$IN")`[0]	@ broadcast lowest byte
	b		.Linner_neon
.size	gcm_gmult_neon,.-gcm_gmult_neon

.global	gcm_ghash_neon
.type	gcm_ghash_neon,%function
.align	4
gcm_ghash_neon:
	vld1.64		`&Dhi("$Z")`,[$Xi,:64]!	@ load Xi
	vmov.i32	$mod,#0xe1		@ our irreducible polynomial
	vld1.64		`&Dlo("$Z")`,[$Xi,:64]!
	vshr.u64	$mod,#32
	vldmia		$Xi,{$Hhi-$Hlo}		@ load H
	veor		$zero,$zero
	nop
#ifdef __ARMEL__
	vrev64.8	$Z,$Z
#endif
.Louter_neon:
	vld1.64		`&Dhi($IN)`,[$inp]!	@ load inp
	veor		$Qpost,$Qpost
	vld1.64		`&Dlo($IN)`,[$inp]!
	veor		$R,$R
	mov		$cnt,#16
#ifdef __ARMEL__
	vrev64.8	$IN,$IN
#endif
	veor		$Zo,$Zo
	veor		$IN,$Z			@ inp^=Xi
	veor		$Z,$Z
	vdup.8		$xi,`&Dlo("$IN")`[0]	@ broadcast lowest byte
.Linner_neon:
	subs		$cnt,$cnt,#1
	vmull.p8	$Qlo,$Hlo,$xi		@ H.lo·Xi[i]
	vmull.p8	$Qhi,$Hhi,$xi		@ H.hi·Xi[i]
	vext.8		$IN,$zero,#1		@ IN>>=8

	veor		$Z,$Qpost		@ modulo-scheduled part
	vshl.i64	`&Dlo("$R")`,#48
	vdup.8		$xi,`&Dlo("$IN")`[0]	@ broadcast lowest byte
	veor		$T,`&Dlo("$Qlo")`,`&Dlo("$Z")`

	veor		`&Dhi("$Z")`,`&Dlo("$R")`
	vuzp.8		$Qlo,$Qhi
	vsli.8		$Zo,$T,#1		@ compose the "carry" byte
	vext.8		$Z,$zero,#1		@ Z>>=8

	vmull.p8	$R,$Zo,$mod		@ "carry"·0xe1
	vshr.u8		$Zo,$T,#7		@ save Z's bottom bit
	vext.8		$Qpost,$Qlo,$zero,#1	@ Qlo>>=8
	veor		$Z,$Qhi
	bne		.Linner_neon

	veor		$Z,$Qpost		@ modulo-scheduled artefact
	vshl.i64	`&Dlo("$R")`,#48
	veor		`&Dhi("$Z")`,`&Dlo("$R")`

	@ finalization, normalize Z:Zo
	vand		$Zo,$mod		@ suffices to mask the bit
	vshr.u64	`&Dhi(&Q("$Zo"))`,`&Dlo("$Z")`,#63
	vshl.i64	$Z,#1
	subs		$len,#16
	vorr		$Z,`&Q("$Zo")`		@ Z=Z:Zo<<1
	bne		.Louter_neon

#ifdef __ARMEL__
	vrev64.8	$Z,$Z
#endif
	sub		$Xi,#16	
	vst1.64		`&Dhi("$Z")`,[$Xi,:64]!	@ write out Xi
	vst1.64		`&Dlo("$Z")`,[$Xi,:64]

	bx	lr
.size	gcm_ghash_neon,.-gcm_ghash_neon
#endif
___
}
$code.=<<___;
.asciz  "GHASH for ARMv4/NEON, CRYPTOGAMS by <appro\@openssl.org>"
.align  2
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/\bbx\s+lr\b/.word\t0xe12fff1e/gm;	# make it possible to compile with -march=armv4
print $code;
close STDOUT; # enforce flush
