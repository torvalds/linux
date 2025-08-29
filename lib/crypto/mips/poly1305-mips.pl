#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause
#
# ====================================================================
# Written by Andy Polyakov, @dot-asm, originally for the OpenSSL
# project.
# ====================================================================

# Poly1305 hash for MIPS.
#
# May 2016
#
# Numbers are cycles per processed byte with poly1305_blocks alone.
#
#		IALU/gcc
# R1x000	~5.5/+130%	(big-endian)
# Octeon II	2.50/+70%	(little-endian)
#
# March 2019
#
# Add 32-bit code path.
#
# October 2019
#
# Modulo-scheduling reduction allows to omit dependency chain at the
# end of inner loop and improve performance. Also optimize MIPS32R2
# code path for MIPS 1004K core. Per René von Dorst's suggestions.
#
#		IALU/gcc
# R1x000	~9.8/?		(big-endian)
# Octeon II	3.65/+140%	(little-endian)
# MT7621/1004K	4.75/?		(little-endian)
#
######################################################################
# There is a number of MIPS ABI in use, O32 and N32/64 are most
# widely used. Then there is a new contender: NUBI. It appears that if
# one picks the latter, it's possible to arrange code in ABI neutral
# manner. Therefore let's stick to NUBI register layout:
#
($zero,$at,$t0,$t1,$t2)=map("\$$_",(0..2,24,25));
($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7,$s8,$s9,$s10,$s11)=map("\$$_",(12..23));
($gp,$tp,$sp,$fp,$ra)=map("\$$_",(3,28..31));
#
# The return value is placed in $a0. Following coding rules facilitate
# interoperability:
#
# - never ever touch $tp, "thread pointer", former $gp [o32 can be
#   excluded from the rule, because it's specified volatile];
# - copy return value to $t0, former $v0 [or to $a0 if you're adapting
#   old code];
# - on O32 populate $a4-$a7 with 'lw $aN,4*N($sp)' if necessary;
#
# For reference here is register layout for N32/64 MIPS ABIs:
#
# ($zero,$at,$v0,$v1)=map("\$$_",(0..3));
# ($a0,$a1,$a2,$a3,$a4,$a5,$a6,$a7)=map("\$$_",(4..11));
# ($t0,$t1,$t2,$t3,$t8,$t9)=map("\$$_",(12..15,24,25));
# ($s0,$s1,$s2,$s3,$s4,$s5,$s6,$s7)=map("\$$_",(16..23));
# ($gp,$sp,$fp,$ra)=map("\$$_",(28..31));
#
# <appro@openssl.org>
#
######################################################################

$flavour = shift || "64"; # supported flavours are o32,n32,64,nubi32,nubi64

$v0 = ($flavour =~ /nubi/i) ? $a0 : $t0;

if ($flavour =~ /64|n32/i) {{{
######################################################################
# 64-bit code path
#

my ($ctx,$inp,$len,$padbit) = ($a0,$a1,$a2,$a3);
my ($in0,$in1,$tmp0,$tmp1,$tmp2,$tmp3,$tmp4) = ($a4,$a5,$a6,$a7,$at,$t0,$t1);

$code.=<<___;
#if (defined(_MIPS_ARCH_MIPS64R3) || defined(_MIPS_ARCH_MIPS64R5) || \\
     defined(_MIPS_ARCH_MIPS64R6)) \\
     && !defined(_MIPS_ARCH_MIPS64R2)
# define _MIPS_ARCH_MIPS64R2
#endif

#if defined(_MIPS_ARCH_MIPS64R6)
# define dmultu(rs,rt)
# define mflo(rd,rs,rt)	dmulu	rd,rs,rt
# define mfhi(rd,rs,rt)	dmuhu	rd,rs,rt
#else
# define dmultu(rs,rt)		dmultu	rs,rt
# define mflo(rd,rs,rt)	mflo	rd
# define mfhi(rd,rs,rt)	mfhi	rd
#endif

#ifdef	__KERNEL__
# define poly1305_init   poly1305_block_init
#endif

#if defined(__MIPSEB__) && !defined(MIPSEB)
# define MIPSEB
#endif

#ifdef MIPSEB
# define MSB 0
# define LSB 7
#else
# define MSB 7
# define LSB 0
#endif

.text
.set	noat
.set	noreorder

.align	5
.globl	poly1305_init
.ent	poly1305_init
poly1305_init:
	.frame	$sp,0,$ra
	.set	reorder

	sd	$zero,0($ctx)
	sd	$zero,8($ctx)
	sd	$zero,16($ctx)

	beqz	$inp,.Lno_key

#if defined(_MIPS_ARCH_MIPS64R6)
	andi	$tmp0,$inp,7		# $inp % 8
	dsubu	$inp,$inp,$tmp0		# align $inp
	sll	$tmp0,$tmp0,3		# byte to bit offset
	ld	$in0,0($inp)
	ld	$in1,8($inp)
	beqz	$tmp0,.Laligned_key
	ld	$tmp2,16($inp)

	subu	$tmp1,$zero,$tmp0
# ifdef	MIPSEB
	dsllv	$in0,$in0,$tmp0
	dsrlv	$tmp3,$in1,$tmp1
	dsllv	$in1,$in1,$tmp0
	dsrlv	$tmp2,$tmp2,$tmp1
# else
	dsrlv	$in0,$in0,$tmp0
	dsllv	$tmp3,$in1,$tmp1
	dsrlv	$in1,$in1,$tmp0
	dsllv	$tmp2,$tmp2,$tmp1
# endif
	or	$in0,$in0,$tmp3
	or	$in1,$in1,$tmp2
.Laligned_key:
#else
	ldl	$in0,0+MSB($inp)
	ldl	$in1,8+MSB($inp)
	ldr	$in0,0+LSB($inp)
	ldr	$in1,8+LSB($inp)
#endif
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS64R2)
	dsbh	$in0,$in0		# byte swap
	 dsbh	$in1,$in1
	dshd	$in0,$in0
	 dshd	$in1,$in1
# else
	ori	$tmp0,$zero,0xFF
	dsll	$tmp2,$tmp0,32
	or	$tmp0,$tmp2		# 0x000000FF000000FF

	and	$tmp1,$in0,$tmp0	# byte swap
	 and	$tmp3,$in1,$tmp0
	dsrl	$tmp2,$in0,24
	 dsrl	$tmp4,$in1,24
	dsll	$tmp1,24
	 dsll	$tmp3,24
	and	$tmp2,$tmp0
	 and	$tmp4,$tmp0
	dsll	$tmp0,8			# 0x0000FF000000FF00
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	and	$tmp2,$in0,$tmp0
	 and	$tmp4,$in1,$tmp0
	dsrl	$in0,8
	 dsrl	$in1,8
	dsll	$tmp2,8
	 dsll	$tmp4,8
	and	$in0,$tmp0
	 and	$in1,$tmp0
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	or	$in0,$tmp1
	 or	$in1,$tmp3
	dsrl	$tmp1,$in0,32
	 dsrl	$tmp3,$in1,32
	dsll	$in0,32
	 dsll	$in1,32
	or	$in0,$tmp1
	 or	$in1,$tmp3
# endif
#endif
	li	$tmp0,1
	dsll	$tmp0,32		# 0x0000000100000000
	daddiu	$tmp0,-63		# 0x00000000ffffffc1
	dsll	$tmp0,28		# 0x0ffffffc10000000
	daddiu	$tmp0,-1		# 0x0ffffffc0fffffff

	and	$in0,$tmp0
	daddiu	$tmp0,-3		# 0x0ffffffc0ffffffc
	and	$in1,$tmp0

	sd	$in0,24($ctx)
	dsrl	$tmp0,$in1,2
	sd	$in1,32($ctx)
	daddu	$tmp0,$in1		# s1 = r1 + (r1 >> 2)
	sd	$tmp0,40($ctx)

.Lno_key:
	li	$v0,0			# return 0
	jr	$ra
.end	poly1305_init
___
{
my $SAVED_REGS_MASK = ($flavour =~ /nubi/i) ? "0x0003f000" : "0x00030000";

my ($h0,$h1,$h2,$r0,$r1,$rs1,$d0,$d1,$d2) =
   ($s0,$s1,$s2,$s3,$s4,$s5,$in0,$in1,$t2);
my ($shr,$shl) = ($s6,$s7);		# used on R6

$code.=<<___;
.align	5
.globl	poly1305_blocks
.ent	poly1305_blocks
poly1305_blocks:
	.set	noreorder
	dsrl	$len,4			# number of complete blocks
	bnez	$len,poly1305_blocks_internal
	nop
	jr	$ra
	nop
.end	poly1305_blocks

.align	5
.ent	poly1305_blocks_internal
poly1305_blocks_internal:
	.set	noreorder
#if defined(_MIPS_ARCH_MIPS64R6)
	.frame	$sp,8*8,$ra
	.mask	$SAVED_REGS_MASK|0x000c0000,-8
	dsubu	$sp,8*8
	sd	$s7,56($sp)
	sd	$s6,48($sp)
#else
	.frame	$sp,6*8,$ra
	.mask	$SAVED_REGS_MASK,-8
	dsubu	$sp,6*8
#endif
	sd	$s5,40($sp)
	sd	$s4,32($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	sd	$s3,24($sp)
	sd	$s2,16($sp)
	sd	$s1,8($sp)
	sd	$s0,0($sp)
___
$code.=<<___;
	.set	reorder

#if defined(_MIPS_ARCH_MIPS64R6)
	andi	$shr,$inp,7
	dsubu	$inp,$inp,$shr		# align $inp
	sll	$shr,$shr,3		# byte to bit offset
	subu	$shl,$zero,$shr
#endif

	ld	$h0,0($ctx)		# load hash value
	ld	$h1,8($ctx)
	ld	$h2,16($ctx)

	ld	$r0,24($ctx)		# load key
	ld	$r1,32($ctx)
	ld	$rs1,40($ctx)

	dsll	$len,4
	daddu	$len,$inp		# end of buffer
	b	.Loop

.align	4
.Loop:
#if defined(_MIPS_ARCH_MIPS64R6)
	ld	$in0,0($inp)		# load input
	ld	$in1,8($inp)
	beqz	$shr,.Laligned_inp

	ld	$tmp2,16($inp)
# ifdef	MIPSEB
	dsllv	$in0,$in0,$shr
	dsrlv	$tmp3,$in1,$shl
	dsllv	$in1,$in1,$shr
	dsrlv	$tmp2,$tmp2,$shl
# else
	dsrlv	$in0,$in0,$shr
	dsllv	$tmp3,$in1,$shl
	dsrlv	$in1,$in1,$shr
	dsllv	$tmp2,$tmp2,$shl
# endif
	or	$in0,$in0,$tmp3
	or	$in1,$in1,$tmp2
.Laligned_inp:
#else
	ldl	$in0,0+MSB($inp)	# load input
	ldl	$in1,8+MSB($inp)
	ldr	$in0,0+LSB($inp)
	ldr	$in1,8+LSB($inp)
#endif
	daddiu	$inp,16
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS64R2)
	dsbh	$in0,$in0		# byte swap
	 dsbh	$in1,$in1
	dshd	$in0,$in0
	 dshd	$in1,$in1
# else
	ori	$tmp0,$zero,0xFF
	dsll	$tmp2,$tmp0,32
	or	$tmp0,$tmp2		# 0x000000FF000000FF

	and	$tmp1,$in0,$tmp0	# byte swap
	 and	$tmp3,$in1,$tmp0
	dsrl	$tmp2,$in0,24
	 dsrl	$tmp4,$in1,24
	dsll	$tmp1,24
	 dsll	$tmp3,24
	and	$tmp2,$tmp0
	 and	$tmp4,$tmp0
	dsll	$tmp0,8			# 0x0000FF000000FF00
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	and	$tmp2,$in0,$tmp0
	 and	$tmp4,$in1,$tmp0
	dsrl	$in0,8
	 dsrl	$in1,8
	dsll	$tmp2,8
	 dsll	$tmp4,8
	and	$in0,$tmp0
	 and	$in1,$tmp0
	or	$tmp1,$tmp2
	 or	$tmp3,$tmp4
	or	$in0,$tmp1
	 or	$in1,$tmp3
	dsrl	$tmp1,$in0,32
	 dsrl	$tmp3,$in1,32
	dsll	$in0,32
	 dsll	$in1,32
	or	$in0,$tmp1
	 or	$in1,$tmp3
# endif
#endif
	dsrl	$tmp1,$h2,2		# modulo-scheduled reduction
	andi	$h2,$h2,3
	dsll	$tmp0,$tmp1,2

	daddu	$d0,$h0,$in0		# accumulate input
	 daddu	$tmp1,$tmp0
	sltu	$tmp0,$d0,$h0
	daddu	$d0,$d0,$tmp1		# ... and residue
	sltu	$tmp1,$d0,$tmp1
	daddu	$d1,$h1,$in1
	daddu	$tmp0,$tmp1
	sltu	$tmp1,$d1,$h1
	daddu	$d1,$tmp0

	dmultu	($r0,$d0)		# h0*r0
	 daddu	$d2,$h2,$padbit
	 sltu	$tmp0,$d1,$tmp0
	mflo	($h0,$r0,$d0)
	mfhi	($h1,$r0,$d0)

	dmultu	($rs1,$d1)		# h1*5*r1
	 daddu	$d2,$tmp1
	 daddu	$d2,$tmp0
	mflo	($tmp0,$rs1,$d1)
	mfhi	($tmp1,$rs1,$d1)

	dmultu	($r1,$d0)		# h0*r1
	mflo	($tmp2,$r1,$d0)
	mfhi	($h2,$r1,$d0)
	 daddu	$h0,$tmp0
	 daddu	$h1,$tmp1
	 sltu	$tmp0,$h0,$tmp0

	dmultu	($r0,$d1)		# h1*r0
	 daddu	$h1,$tmp0
	 daddu	$h1,$tmp2
	mflo	($tmp0,$r0,$d1)
	mfhi	($tmp1,$r0,$d1)

	dmultu	($rs1,$d2)		# h2*5*r1
	 sltu	$tmp2,$h1,$tmp2
	 daddu	$h2,$tmp2
	mflo	($tmp2,$rs1,$d2)

	dmultu	($r0,$d2)		# h2*r0
	 daddu	$h1,$tmp0
	 daddu	$h2,$tmp1
	mflo	($tmp3,$r0,$d2)
	 sltu	$tmp0,$h1,$tmp0
	 daddu	$h2,$tmp0

	daddu	$h1,$tmp2
	sltu	$tmp2,$h1,$tmp2
	daddu	$h2,$tmp2
	daddu	$h2,$tmp3

	bne	$inp,$len,.Loop

	sd	$h0,0($ctx)		# store hash value
	sd	$h1,8($ctx)
	sd	$h2,16($ctx)

	.set	noreorder
#if defined(_MIPS_ARCH_MIPS64R6)
	ld	$s7,56($sp)
	ld	$s6,48($sp)
#endif
	ld	$s5,40($sp)		# epilogue
	ld	$s4,32($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi epilogue
	ld	$s3,24($sp)
	ld	$s2,16($sp)
	ld	$s1,8($sp)
	ld	$s0,0($sp)
___
$code.=<<___;
	jr	$ra
#if defined(_MIPS_ARCH_MIPS64R6)
	daddu	$sp,8*8
#else
	daddu	$sp,6*8
#endif
.end	poly1305_blocks_internal
___
}
{
my ($ctx,$mac,$nonce) = ($a0,$a1,$a2);

$code.=<<___;
.align	5
.globl	poly1305_emit
.ent	poly1305_emit
poly1305_emit:
	.frame	$sp,0,$ra
	.set	reorder

	ld	$tmp2,16($ctx)
	ld	$tmp0,0($ctx)
	ld	$tmp1,8($ctx)

	li	$in0,-4			# final reduction
	dsrl	$in1,$tmp2,2
	and	$in0,$tmp2
	andi	$tmp2,$tmp2,3
	daddu	$in0,$in1

	daddu	$tmp0,$tmp0,$in0
	sltu	$in1,$tmp0,$in0
	 daddiu	$in0,$tmp0,5		# compare to modulus
	daddu	$tmp1,$tmp1,$in1
	 sltiu	$tmp3,$in0,5
	sltu	$tmp4,$tmp1,$in1
	 daddu	$in1,$tmp1,$tmp3
	daddu	$tmp2,$tmp2,$tmp4
	 sltu	$tmp3,$in1,$tmp3
	 daddu	$tmp2,$tmp2,$tmp3

	dsrl	$tmp2,2			# see if it carried/borrowed
	dsubu	$tmp2,$zero,$tmp2

	xor	$in0,$tmp0
	xor	$in1,$tmp1
	and	$in0,$tmp2
	and	$in1,$tmp2
	xor	$in0,$tmp0
	xor	$in1,$tmp1

	lwu	$tmp0,0($nonce)		# load nonce
	lwu	$tmp1,4($nonce)
	lwu	$tmp2,8($nonce)
	lwu	$tmp3,12($nonce)
	dsll	$tmp1,32
	dsll	$tmp3,32
	or	$tmp0,$tmp1
	or	$tmp2,$tmp3

	daddu	$in0,$tmp0		# accumulate nonce
	daddu	$in1,$tmp2
	sltu	$tmp0,$in0,$tmp0
	daddu	$in1,$tmp0

	dsrl	$tmp0,$in0,8		# write mac value
	dsrl	$tmp1,$in0,16
	dsrl	$tmp2,$in0,24
	sb	$in0,0($mac)
	dsrl	$tmp3,$in0,32
	sb	$tmp0,1($mac)
	dsrl	$tmp0,$in0,40
	sb	$tmp1,2($mac)
	dsrl	$tmp1,$in0,48
	sb	$tmp2,3($mac)
	dsrl	$tmp2,$in0,56
	sb	$tmp3,4($mac)
	dsrl	$tmp3,$in1,8
	sb	$tmp0,5($mac)
	dsrl	$tmp0,$in1,16
	sb	$tmp1,6($mac)
	dsrl	$tmp1,$in1,24
	sb	$tmp2,7($mac)

	sb	$in1,8($mac)
	dsrl	$tmp2,$in1,32
	sb	$tmp3,9($mac)
	dsrl	$tmp3,$in1,40
	sb	$tmp0,10($mac)
	dsrl	$tmp0,$in1,48
	sb	$tmp1,11($mac)
	dsrl	$tmp1,$in1,56
	sb	$tmp2,12($mac)
	sb	$tmp3,13($mac)
	sb	$tmp0,14($mac)
	sb	$tmp1,15($mac)

	jr	$ra
.end	poly1305_emit
.rdata
.asciiz	"Poly1305 for MIPS64, CRYPTOGAMS by \@dot-asm"
.align	2
___
}
}}} else {{{
######################################################################
# 32-bit code path
#

my ($ctx,$inp,$len,$padbit) = ($a0,$a1,$a2,$a3);
my ($in0,$in1,$in2,$in3,$tmp0,$tmp1,$tmp2,$tmp3) =
   ($a4,$a5,$a6,$a7,$at,$t0,$t1,$t2);

$code.=<<___;
#if (defined(_MIPS_ARCH_MIPS32R3) || defined(_MIPS_ARCH_MIPS32R5) || \\
     defined(_MIPS_ARCH_MIPS32R6)) \\
     && !defined(_MIPS_ARCH_MIPS32R2)
# define _MIPS_ARCH_MIPS32R2
#endif

#if defined(_MIPS_ARCH_MIPS32R6)
# define multu(rs,rt)
# define mflo(rd,rs,rt)	mulu	rd,rs,rt
# define mfhi(rd,rs,rt)	muhu	rd,rs,rt
#else
# define multu(rs,rt)	multu	rs,rt
# define mflo(rd,rs,rt)	mflo	rd
# define mfhi(rd,rs,rt)	mfhi	rd
#endif

#ifdef	__KERNEL__
# define poly1305_init   poly1305_block_init
#endif

#if defined(__MIPSEB__) && !defined(MIPSEB)
# define MIPSEB
#endif

#ifdef MIPSEB
# define MSB 0
# define LSB 3
#else
# define MSB 3
# define LSB 0
#endif

.text
.set	noat
.set	noreorder

.align	5
.globl	poly1305_init
.ent	poly1305_init
poly1305_init:
	.frame	$sp,0,$ra
	.set	reorder

	sw	$zero,0($ctx)
	sw	$zero,4($ctx)
	sw	$zero,8($ctx)
	sw	$zero,12($ctx)
	sw	$zero,16($ctx)

	beqz	$inp,.Lno_key

#if defined(_MIPS_ARCH_MIPS32R6)
	andi	$tmp0,$inp,3		# $inp % 4
	subu	$inp,$inp,$tmp0		# align $inp
	sll	$tmp0,$tmp0,3		# byte to bit offset
	lw	$in0,0($inp)
	lw	$in1,4($inp)
	lw	$in2,8($inp)
	lw	$in3,12($inp)
	beqz	$tmp0,.Laligned_key

	lw	$tmp2,16($inp)
	subu	$tmp1,$zero,$tmp0
# ifdef	MIPSEB
	sllv	$in0,$in0,$tmp0
	srlv	$tmp3,$in1,$tmp1
	sllv	$in1,$in1,$tmp0
	or	$in0,$in0,$tmp3
	srlv	$tmp3,$in2,$tmp1
	sllv	$in2,$in2,$tmp0
	or	$in1,$in1,$tmp3
	srlv	$tmp3,$in3,$tmp1
	sllv	$in3,$in3,$tmp0
	or	$in2,$in2,$tmp3
	srlv	$tmp2,$tmp2,$tmp1
	or	$in3,$in3,$tmp2
# else
	srlv	$in0,$in0,$tmp0
	sllv	$tmp3,$in1,$tmp1
	srlv	$in1,$in1,$tmp0
	or	$in0,$in0,$tmp3
	sllv	$tmp3,$in2,$tmp1
	srlv	$in2,$in2,$tmp0
	or	$in1,$in1,$tmp3
	sllv	$tmp3,$in3,$tmp1
	srlv	$in3,$in3,$tmp0
	or	$in2,$in2,$tmp3
	sllv	$tmp2,$tmp2,$tmp1
	or	$in3,$in3,$tmp2
# endif
.Laligned_key:
#else
	lwl	$in0,0+MSB($inp)
	lwl	$in1,4+MSB($inp)
	lwl	$in2,8+MSB($inp)
	lwl	$in3,12+MSB($inp)
	lwr	$in0,0+LSB($inp)
	lwr	$in1,4+LSB($inp)
	lwr	$in2,8+LSB($inp)
	lwr	$in3,12+LSB($inp)
#endif
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS32R2)
	wsbh	$in0,$in0		# byte swap
	wsbh	$in1,$in1
	wsbh	$in2,$in2
	wsbh	$in3,$in3
	rotr	$in0,$in0,16
	rotr	$in1,$in1,16
	rotr	$in2,$in2,16
	rotr	$in3,$in3,16
# else
	srl	$tmp0,$in0,24		# byte swap
	srl	$tmp1,$in0,8
	andi	$tmp2,$in0,0xFF00
	sll	$in0,$in0,24
	andi	$tmp1,0xFF00
	sll	$tmp2,$tmp2,8
	or	$in0,$tmp0
	 srl	$tmp0,$in1,24
	or	$tmp1,$tmp2
	 srl	$tmp2,$in1,8
	or	$in0,$tmp1
	 andi	$tmp1,$in1,0xFF00
	 sll	$in1,$in1,24
	 andi	$tmp2,0xFF00
	 sll	$tmp1,$tmp1,8
	 or	$in1,$tmp0
	srl	$tmp0,$in2,24
	 or	$tmp2,$tmp1
	srl	$tmp1,$in2,8
	 or	$in1,$tmp2
	andi	$tmp2,$in2,0xFF00
	sll	$in2,$in2,24
	andi	$tmp1,0xFF00
	sll	$tmp2,$tmp2,8
	or	$in2,$tmp0
	 srl	$tmp0,$in3,24
	or	$tmp1,$tmp2
	 srl	$tmp2,$in3,8
	or	$in2,$tmp1
	 andi	$tmp1,$in3,0xFF00
	 sll	$in3,$in3,24
	 andi	$tmp2,0xFF00
	 sll	$tmp1,$tmp1,8
	 or	$in3,$tmp0
	 or	$tmp2,$tmp1
	 or	$in3,$tmp2
# endif
#endif
	lui	$tmp0,0x0fff
	ori	$tmp0,0xffff		# 0x0fffffff
	and	$in0,$in0,$tmp0
	subu	$tmp0,3			# 0x0ffffffc
	and	$in1,$in1,$tmp0
	and	$in2,$in2,$tmp0
	and	$in3,$in3,$tmp0

	sw	$in0,20($ctx)
	sw	$in1,24($ctx)
	sw	$in2,28($ctx)
	sw	$in3,32($ctx)

	srl	$tmp1,$in1,2
	srl	$tmp2,$in2,2
	srl	$tmp3,$in3,2
	addu	$in1,$in1,$tmp1		# s1 = r1 + (r1 >> 2)
	addu	$in2,$in2,$tmp2
	addu	$in3,$in3,$tmp3
	sw	$in1,36($ctx)
	sw	$in2,40($ctx)
	sw	$in3,44($ctx)
.Lno_key:
	li	$v0,0
	jr	$ra
.end	poly1305_init
___
{
my $SAVED_REGS_MASK = ($flavour =~ /nubi/i) ? "0x00fff000" : "0x00ff0000";

my ($h0,$h1,$h2,$h3,$h4, $r0,$r1,$r2,$r3, $rs1,$rs2,$rs3) =
   ($s0,$s1,$s2,$s3,$s4, $s5,$s6,$s7,$s8, $s9,$s10,$s11);
my ($d0,$d1,$d2,$d3) =
   ($a4,$a5,$a6,$a7);
my $shr = $t2;		# used on R6
my $one = $t2;		# used on R2

$code.=<<___;
.globl	poly1305_blocks
.align	5
.ent	poly1305_blocks
poly1305_blocks:
	.frame	$sp,16*4,$ra
	.mask	$SAVED_REGS_MASK,-4
	.set	noreorder
	subu	$sp, $sp,4*12
	sw	$s11,4*11($sp)
	sw	$s10,4*10($sp)
	sw	$s9, 4*9($sp)
	sw	$s8, 4*8($sp)
	sw	$s7, 4*7($sp)
	sw	$s6, 4*6($sp)
	sw	$s5, 4*5($sp)
	sw	$s4, 4*4($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	sw	$s3, 4*3($sp)
	sw	$s2, 4*2($sp)
	sw	$s1, 4*1($sp)
	sw	$s0, 4*0($sp)
___
$code.=<<___;
	.set	reorder

	srl	$len,4			# number of complete blocks
	li	$one,1
	beqz	$len,.Labort

#if defined(_MIPS_ARCH_MIPS32R6)
	andi	$shr,$inp,3
	subu	$inp,$inp,$shr		# align $inp
	sll	$shr,$shr,3		# byte to bit offset
#endif

	lw	$h0,0($ctx)		# load hash value
	lw	$h1,4($ctx)
	lw	$h2,8($ctx)
	lw	$h3,12($ctx)
	lw	$h4,16($ctx)

	lw	$r0,20($ctx)		# load key
	lw	$r1,24($ctx)
	lw	$r2,28($ctx)
	lw	$r3,32($ctx)
	lw	$rs1,36($ctx)
	lw	$rs2,40($ctx)
	lw	$rs3,44($ctx)

	sll	$len,4
	addu	$len,$len,$inp		# end of buffer
	b	.Loop

.align	4
.Loop:
#if defined(_MIPS_ARCH_MIPS32R6)
	lw	$d0,0($inp)		# load input
	lw	$d1,4($inp)
	lw	$d2,8($inp)
	lw	$d3,12($inp)
	beqz	$shr,.Laligned_inp

	lw	$t0,16($inp)
	subu	$t1,$zero,$shr
# ifdef	MIPSEB
	sllv	$d0,$d0,$shr
	srlv	$at,$d1,$t1
	sllv	$d1,$d1,$shr
	or	$d0,$d0,$at
	srlv	$at,$d2,$t1
	sllv	$d2,$d2,$shr
	or	$d1,$d1,$at
	srlv	$at,$d3,$t1
	sllv	$d3,$d3,$shr
	or	$d2,$d2,$at
	srlv	$t0,$t0,$t1
	or	$d3,$d3,$t0
# else
	srlv	$d0,$d0,$shr
	sllv	$at,$d1,$t1
	srlv	$d1,$d1,$shr
	or	$d0,$d0,$at
	sllv	$at,$d2,$t1
	srlv	$d2,$d2,$shr
	or	$d1,$d1,$at
	sllv	$at,$d3,$t1
	srlv	$d3,$d3,$shr
	or	$d2,$d2,$at
	sllv	$t0,$t0,$t1
	or	$d3,$d3,$t0
# endif
.Laligned_inp:
#else
	lwl	$d0,0+MSB($inp)		# load input
	lwl	$d1,4+MSB($inp)
	lwl	$d2,8+MSB($inp)
	lwl	$d3,12+MSB($inp)
	lwr	$d0,0+LSB($inp)
	lwr	$d1,4+LSB($inp)
	lwr	$d2,8+LSB($inp)
	lwr	$d3,12+LSB($inp)
#endif
#ifdef	MIPSEB
# if defined(_MIPS_ARCH_MIPS32R2)
	wsbh	$d0,$d0			# byte swap
	wsbh	$d1,$d1
	wsbh	$d2,$d2
	wsbh	$d3,$d3
	rotr	$d0,$d0,16
	rotr	$d1,$d1,16
	rotr	$d2,$d2,16
	rotr	$d3,$d3,16
# else
	srl	$at,$d0,24		# byte swap
	srl	$t0,$d0,8
	andi	$t1,$d0,0xFF00
	sll	$d0,$d0,24
	andi	$t0,0xFF00
	sll	$t1,$t1,8
	or	$d0,$at
	 srl	$at,$d1,24
	or	$t0,$t1
	 srl	$t1,$d1,8
	or	$d0,$t0
	 andi	$t0,$d1,0xFF00
	 sll	$d1,$d1,24
	 andi	$t1,0xFF00
	 sll	$t0,$t0,8
	 or	$d1,$at
	srl	$at,$d2,24
	 or	$t1,$t0
	srl	$t0,$d2,8
	 or	$d1,$t1
	andi	$t1,$d2,0xFF00
	sll	$d2,$d2,24
	andi	$t0,0xFF00
	sll	$t1,$t1,8
	or	$d2,$at
	 srl	$at,$d3,24
	or	$t0,$t1
	 srl	$t1,$d3,8
	or	$d2,$t0
	 andi	$t0,$d3,0xFF00
	 sll	$d3,$d3,24
	 andi	$t1,0xFF00
	 sll	$t0,$t0,8
	 or	$d3,$at
	 or	$t1,$t0
	 or	$d3,$t1
# endif
#endif
	srl	$t0,$h4,2		# modulo-scheduled reduction
	andi	$h4,$h4,3
	sll	$at,$t0,2

	addu	$d0,$d0,$h0		# accumulate input
	 addu	$t0,$t0,$at
	sltu	$h0,$d0,$h0
	addu	$d0,$d0,$t0		# ... and residue
	sltu	$at,$d0,$t0

	addu	$d1,$d1,$h1
	 addu	$h0,$h0,$at		# carry
	sltu	$h1,$d1,$h1
	addu	$d1,$d1,$h0
	sltu	$h0,$d1,$h0

	addu	$d2,$d2,$h2
	 addu	$h1,$h1,$h0		# carry
	sltu	$h2,$d2,$h2
	addu	$d2,$d2,$h1
	sltu	$h1,$d2,$h1

	addu	$d3,$d3,$h3
	 addu	$h2,$h2,$h1		# carry
	sltu	$h3,$d3,$h3
	addu	$d3,$d3,$h2

#if defined(_MIPS_ARCH_MIPS32R2) && !defined(_MIPS_ARCH_MIPS32R6)
	multu	$r0,$d0			# d0*r0
	 sltu	$h2,$d3,$h2
	maddu	$rs3,$d1		# d1*s3
	 addu	$h3,$h3,$h2		# carry
	maddu	$rs2,$d2		# d2*s2
	 addu	$h4,$h4,$padbit
	maddu	$rs1,$d3		# d3*s1
	 addu	$h4,$h4,$h3
	mfhi	$at
	mflo	$h0

	multu	$r1,$d0			# d0*r1
	maddu	$r0,$d1			# d1*r0
	maddu	$rs3,$d2		# d2*s3
	maddu	$rs2,$d3		# d3*s2
	maddu	$rs1,$h4		# h4*s1
	maddu	$at,$one		# hi*1
	mfhi	$at
	mflo	$h1

	multu	$r2,$d0			# d0*r2
	maddu	$r1,$d1			# d1*r1
	maddu	$r0,$d2			# d2*r0
	maddu	$rs3,$d3		# d3*s3
	maddu	$rs2,$h4		# h4*s2
	maddu	$at,$one		# hi*1
	mfhi	$at
	mflo	$h2

	mul	$t0,$r0,$h4		# h4*r0

	multu	$r3,$d0			# d0*r3
	maddu	$r2,$d1			# d1*r2
	maddu	$r1,$d2			# d2*r1
	maddu	$r0,$d3			# d3*r0
	maddu	$rs3,$h4		# h4*s3
	maddu	$at,$one		# hi*1
	mfhi	$at
	mflo	$h3

	 addiu	$inp,$inp,16

	addu	$h4,$t0,$at
#else
	multu	($r0,$d0)		# d0*r0
	mflo	($h0,$r0,$d0)
	mfhi	($h1,$r0,$d0)

	 sltu	$h2,$d3,$h2
	 addu	$h3,$h3,$h2		# carry

	multu	($rs3,$d1)		# d1*s3
	mflo	($at,$rs3,$d1)
	mfhi	($t0,$rs3,$d1)

	 addu	$h4,$h4,$padbit
	 addiu	$inp,$inp,16
	 addu	$h4,$h4,$h3

	multu	($rs2,$d2)		# d2*s2
	mflo	($a3,$rs2,$d2)
	mfhi	($t1,$rs2,$d2)
	 addu	$h0,$h0,$at
	 addu	$h1,$h1,$t0
	multu	($rs1,$d3)		# d3*s1
	 sltu	$at,$h0,$at
	 addu	$h1,$h1,$at

	mflo	($at,$rs1,$d3)
	mfhi	($t0,$rs1,$d3)
	 addu	$h0,$h0,$a3
	 addu	$h1,$h1,$t1
	multu	($r1,$d0)		# d0*r1
	 sltu	$a3,$h0,$a3
	 addu	$h1,$h1,$a3


	mflo	($a3,$r1,$d0)
	mfhi	($h2,$r1,$d0)
	 addu	$h0,$h0,$at
	 addu	$h1,$h1,$t0
	multu	($r0,$d1)		# d1*r0
	 sltu	$at,$h0,$at
	 addu	$h1,$h1,$at

	mflo	($at,$r0,$d1)
	mfhi	($t0,$r0,$d1)
	 addu	$h1,$h1,$a3
	 sltu	$a3,$h1,$a3
	multu	($rs3,$d2)		# d2*s3
	 addu	$h2,$h2,$a3

	mflo	($a3,$rs3,$d2)
	mfhi	($t1,$rs3,$d2)
	 addu	$h1,$h1,$at
	 addu	$h2,$h2,$t0
	multu	($rs2,$d3)		# d3*s2
	 sltu	$at,$h1,$at
	 addu	$h2,$h2,$at

	mflo	($at,$rs2,$d3)
	mfhi	($t0,$rs2,$d3)
	 addu	$h1,$h1,$a3
	 addu	$h2,$h2,$t1
	multu	($rs1,$h4)		# h4*s1
	 sltu	$a3,$h1,$a3
	 addu	$h2,$h2,$a3

	mflo	($a3,$rs1,$h4)
	 addu	$h1,$h1,$at
	 addu	$h2,$h2,$t0
	multu	($r2,$d0)		# d0*r2
	 sltu	$at,$h1,$at
	 addu	$h2,$h2,$at


	mflo	($at,$r2,$d0)
	mfhi	($h3,$r2,$d0)
	 addu	$h1,$h1,$a3
	 sltu	$a3,$h1,$a3
	multu	($r1,$d1)		# d1*r1
	 addu	$h2,$h2,$a3

	mflo	($a3,$r1,$d1)
	mfhi	($t1,$r1,$d1)
	 addu	$h2,$h2,$at
	 sltu	$at,$h2,$at
	multu	($r0,$d2)		# d2*r0
	 addu	$h3,$h3,$at

	mflo	($at,$r0,$d2)
	mfhi	($t0,$r0,$d2)
	 addu	$h2,$h2,$a3
	 addu	$h3,$h3,$t1
	multu	($rs3,$d3)		# d3*s3
	 sltu	$a3,$h2,$a3
	 addu	$h3,$h3,$a3

	mflo	($a3,$rs3,$d3)
	mfhi	($t1,$rs3,$d3)
	 addu	$h2,$h2,$at
	 addu	$h3,$h3,$t0
	multu	($rs2,$h4)		# h4*s2
	 sltu	$at,$h2,$at
	 addu	$h3,$h3,$at

	mflo	($at,$rs2,$h4)
	 addu	$h2,$h2,$a3
	 addu	$h3,$h3,$t1
	multu	($r3,$d0)		# d0*r3
	 sltu	$a3,$h2,$a3
	 addu	$h3,$h3,$a3


	mflo	($a3,$r3,$d0)
	mfhi	($t1,$r3,$d0)
	 addu	$h2,$h2,$at
	 sltu	$at,$h2,$at
	multu	($r2,$d1)		# d1*r2
	 addu	$h3,$h3,$at

	mflo	($at,$r2,$d1)
	mfhi	($t0,$r2,$d1)
	 addu	$h3,$h3,$a3
	 sltu	$a3,$h3,$a3
	multu	($r0,$d3)		# d3*r0
	 addu	$t1,$t1,$a3

	mflo	($a3,$r0,$d3)
	mfhi	($d3,$r0,$d3)
	 addu	$h3,$h3,$at
	 addu	$t1,$t1,$t0
	multu	($r1,$d2)		# d2*r1
	 sltu	$at,$h3,$at
	 addu	$t1,$t1,$at

	mflo	($at,$r1,$d2)
	mfhi	($t0,$r1,$d2)
	 addu	$h3,$h3,$a3
	 addu	$t1,$t1,$d3
	multu	($rs3,$h4)		# h4*s3
	 sltu	$a3,$h3,$a3
	 addu	$t1,$t1,$a3

	mflo	($a3,$rs3,$h4)
	 addu	$h3,$h3,$at
	 addu	$t1,$t1,$t0
	multu	($r0,$h4)		# h4*r0
	 sltu	$at,$h3,$at
	 addu	$t1,$t1,$at


	mflo	($h4,$r0,$h4)
	 addu	$h3,$h3,$a3
	 sltu	$a3,$h3,$a3
	 addu	$t1,$t1,$a3
	addu	$h4,$h4,$t1

	li	$padbit,1		# if we loop, padbit is 1
#endif
	bne	$inp,$len,.Loop

	sw	$h0,0($ctx)		# store hash value
	sw	$h1,4($ctx)
	sw	$h2,8($ctx)
	sw	$h3,12($ctx)
	sw	$h4,16($ctx)

	.set	noreorder
.Labort:
	lw	$s11,4*11($sp)
	lw	$s10,4*10($sp)
	lw	$s9, 4*9($sp)
	lw	$s8, 4*8($sp)
	lw	$s7, 4*7($sp)
	lw	$s6, 4*6($sp)
	lw	$s5, 4*5($sp)
	lw	$s4, 4*4($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	lw	$s3, 4*3($sp)
	lw	$s2, 4*2($sp)
	lw	$s1, 4*1($sp)
	lw	$s0, 4*0($sp)
___
$code.=<<___;
	jr	$ra
	addu	$sp,$sp,4*12
.end	poly1305_blocks
___
}
{
my ($ctx,$mac,$nonce,$tmp4) = ($a0,$a1,$a2,$a3);

$code.=<<___;
.align	5
.globl	poly1305_emit
.ent	poly1305_emit
poly1305_emit:
	.frame	$sp,0,$ra
	.set	reorder

	lw	$tmp4,16($ctx)
	lw	$tmp0,0($ctx)
	lw	$tmp1,4($ctx)
	lw	$tmp2,8($ctx)
	lw	$tmp3,12($ctx)

	li	$in0,-4			# final reduction
	srl	$ctx,$tmp4,2
	and	$in0,$in0,$tmp4
	andi	$tmp4,$tmp4,3
	addu	$ctx,$ctx,$in0

	addu	$tmp0,$tmp0,$ctx
	sltu	$ctx,$tmp0,$ctx
	 addiu	$in0,$tmp0,5		# compare to modulus
	addu	$tmp1,$tmp1,$ctx
	 sltiu	$in1,$in0,5
	sltu	$ctx,$tmp1,$ctx
	 addu	$in1,$in1,$tmp1
	addu	$tmp2,$tmp2,$ctx
	 sltu	$in2,$in1,$tmp1
	sltu	$ctx,$tmp2,$ctx
	 addu	$in2,$in2,$tmp2
	addu	$tmp3,$tmp3,$ctx
	 sltu	$in3,$in2,$tmp2
	sltu	$ctx,$tmp3,$ctx
	 addu	$in3,$in3,$tmp3
	addu	$tmp4,$tmp4,$ctx
	 sltu	$ctx,$in3,$tmp3
	 addu	$ctx,$tmp4

	srl	$ctx,2			# see if it carried/borrowed
	subu	$ctx,$zero,$ctx

	xor	$in0,$tmp0
	xor	$in1,$tmp1
	xor	$in2,$tmp2
	xor	$in3,$tmp3
	and	$in0,$ctx
	and	$in1,$ctx
	and	$in2,$ctx
	and	$in3,$ctx
	xor	$in0,$tmp0
	xor	$in1,$tmp1
	xor	$in2,$tmp2
	xor	$in3,$tmp3

	lw	$tmp0,0($nonce)		# load nonce
	lw	$tmp1,4($nonce)
	lw	$tmp2,8($nonce)
	lw	$tmp3,12($nonce)

	addu	$in0,$tmp0		# accumulate nonce
	sltu	$ctx,$in0,$tmp0

	addu	$in1,$tmp1
	sltu	$tmp1,$in1,$tmp1
	addu	$in1,$ctx
	sltu	$ctx,$in1,$ctx
	addu	$ctx,$tmp1

	addu	$in2,$tmp2
	sltu	$tmp2,$in2,$tmp2
	addu	$in2,$ctx
	sltu	$ctx,$in2,$ctx
	addu	$ctx,$tmp2

	addu	$in3,$tmp3
	addu	$in3,$ctx

	srl	$tmp0,$in0,8		# write mac value
	srl	$tmp1,$in0,16
	srl	$tmp2,$in0,24
	sb	$in0, 0($mac)
	sb	$tmp0,1($mac)
	srl	$tmp0,$in1,8
	sb	$tmp1,2($mac)
	srl	$tmp1,$in1,16
	sb	$tmp2,3($mac)
	srl	$tmp2,$in1,24
	sb	$in1, 4($mac)
	sb	$tmp0,5($mac)
	srl	$tmp0,$in2,8
	sb	$tmp1,6($mac)
	srl	$tmp1,$in2,16
	sb	$tmp2,7($mac)
	srl	$tmp2,$in2,24
	sb	$in2, 8($mac)
	sb	$tmp0,9($mac)
	srl	$tmp0,$in3,8
	sb	$tmp1,10($mac)
	srl	$tmp1,$in3,16
	sb	$tmp2,11($mac)
	srl	$tmp2,$in3,24
	sb	$in3, 12($mac)
	sb	$tmp0,13($mac)
	sb	$tmp1,14($mac)
	sb	$tmp2,15($mac)

	jr	$ra
.end	poly1305_emit
.rdata
.asciiz	"Poly1305 for MIPS32, CRYPTOGAMS by \@dot-asm"
.align	2
___
}
}}}

$output=pop and open STDOUT,">$output";
print $code;
close STDOUT;
