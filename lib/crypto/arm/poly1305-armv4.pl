#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-1.0+ OR BSD-3-Clause
#
# ====================================================================
# Written by Andy Polyakov, @dot-asm, initially for the OpenSSL
# project.
# ====================================================================
#
#			IALU(*)/gcc-4.4		NEON
#
# ARM11xx(ARMv6)	7.78/+100%		-
# Cortex-A5		6.35/+130%		3.00
# Cortex-A8		6.25/+115%		2.36
# Cortex-A9		5.10/+95%		2.55
# Cortex-A15		3.85/+85%		1.25(**)
# Snapdragon S4		5.70/+100%		1.48(**)
#
# (*)	this is for -march=armv6, i.e. with bunch of ldrb loading data;
# (**)	these are trade-off results, they can be improved by ~8% but at
#	the cost of 15/12% regression on Cortex-A5/A7, it's even possible
#	to improve Cortex-A9 result, but then A5/A7 loose more than 20%;

$flavour = shift;
if ($flavour=~/\w[\w\-]*\.\w+$/) { $output=$flavour; undef $flavour; }
else { while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {} }

if ($flavour && $flavour ne "void") {
    $0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
    ( $xlate="${dir}arm-xlate.pl" and -f $xlate ) or
    ( $xlate="${dir}../../perlasm/arm-xlate.pl" and -f $xlate) or
    die "can't locate arm-xlate.pl";

    open STDOUT,"| \"$^X\" $xlate $flavour $output";
} else {
    open STDOUT,">$output";
}

($ctx,$inp,$len,$padbit)=map("r$_",(0..3));

$code.=<<___;
#ifndef	__KERNEL__
# include "arm_arch.h"
#else
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
# define __ARM_MAX_ARCH__ __LINUX_ARM_ARCH__
# define poly1305_init   poly1305_block_init
# define poly1305_blocks poly1305_blocks_arm
#endif

#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.text

.globl	poly1305_emit
.globl	poly1305_blocks
.globl	poly1305_init
.type	poly1305_init,%function
.align	5
poly1305_init:
.Lpoly1305_init:
	stmdb	sp!,{r4-r11}

	eor	r3,r3,r3
	cmp	$inp,#0
	str	r3,[$ctx,#0]		@ zero hash value
	str	r3,[$ctx,#4]
	str	r3,[$ctx,#8]
	str	r3,[$ctx,#12]
	str	r3,[$ctx,#16]
	str	r3,[$ctx,#36]		@ clear is_base2_26
	add	$ctx,$ctx,#20

#ifdef	__thumb2__
	it	eq
#endif
	moveq	r0,#0
	beq	.Lno_key

#if	__ARM_MAX_ARCH__>=7
	mov	r3,#-1
	str	r3,[$ctx,#28]		@ impossible key power value
# ifndef __KERNEL__
	adr	r11,.Lpoly1305_init
	ldr	r12,.LOPENSSL_armcap
# endif
#endif
	ldrb	r4,[$inp,#0]
	mov	r10,#0x0fffffff
	ldrb	r5,[$inp,#1]
	and	r3,r10,#-4		@ 0x0ffffffc
	ldrb	r6,[$inp,#2]
	ldrb	r7,[$inp,#3]
	orr	r4,r4,r5,lsl#8
	ldrb	r5,[$inp,#4]
	orr	r4,r4,r6,lsl#16
	ldrb	r6,[$inp,#5]
	orr	r4,r4,r7,lsl#24
	ldrb	r7,[$inp,#6]
	and	r4,r4,r10

#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
# if !defined(_WIN32)
	ldr	r12,[r11,r12]		@ OPENSSL_armcap_P
# endif
# if defined(__APPLE__) || defined(_WIN32)
	ldr	r12,[r12]
# endif
#endif
	ldrb	r8,[$inp,#7]
	orr	r5,r5,r6,lsl#8
	ldrb	r6,[$inp,#8]
	orr	r5,r5,r7,lsl#16
	ldrb	r7,[$inp,#9]
	orr	r5,r5,r8,lsl#24
	ldrb	r8,[$inp,#10]
	and	r5,r5,r3

#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	tst	r12,#ARMV7_NEON		@ check for NEON
# ifdef	__thumb2__
	adr	r9,.Lpoly1305_blocks_neon
	adr	r11,.Lpoly1305_blocks
	it	ne
	movne	r11,r9
	adr	r12,.Lpoly1305_emit
	orr	r11,r11,#1		@ thumb-ify addresses
	orr	r12,r12,#1
# else
	add	r12,r11,#(.Lpoly1305_emit-.Lpoly1305_init)
	ite	eq
	addeq	r11,r11,#(.Lpoly1305_blocks-.Lpoly1305_init)
	addne	r11,r11,#(.Lpoly1305_blocks_neon-.Lpoly1305_init)
# endif
#endif
	ldrb	r9,[$inp,#11]
	orr	r6,r6,r7,lsl#8
	ldrb	r7,[$inp,#12]
	orr	r6,r6,r8,lsl#16
	ldrb	r8,[$inp,#13]
	orr	r6,r6,r9,lsl#24
	ldrb	r9,[$inp,#14]
	and	r6,r6,r3

	ldrb	r10,[$inp,#15]
	orr	r7,r7,r8,lsl#8
	str	r4,[$ctx,#0]
	orr	r7,r7,r9,lsl#16
	str	r5,[$ctx,#4]
	orr	r7,r7,r10,lsl#24
	str	r6,[$ctx,#8]
	and	r7,r7,r3
	str	r7,[$ctx,#12]
#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	stmia	r2,{r11,r12}		@ fill functions table
	mov	r0,#1
#else
	mov	r0,#0
#endif
.Lno_key:
	ldmia	sp!,{r4-r11}
#if	__ARM_ARCH__>=5
	ret				@ bx	lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_init,.-poly1305_init
___
{
my ($h0,$h1,$h2,$h3,$h4,$r0,$r1,$r2,$r3)=map("r$_",(4..12));
my ($s1,$s2,$s3)=($r1,$r2,$r3);

$code.=<<___;
.type	poly1305_blocks,%function
.align	5
poly1305_blocks:
.Lpoly1305_blocks:
	stmdb	sp!,{r3-r11,lr}

	ands	$len,$len,#-16
	beq	.Lno_data

	add	$len,$len,$inp		@ end pointer
	sub	sp,sp,#32

#if __ARM_ARCH__<7
	ldmia	$ctx,{$h0-$r3}		@ load context
	add	$ctx,$ctx,#20
	str	$len,[sp,#16]		@ offload stuff
	str	$ctx,[sp,#12]
#else
	ldr	lr,[$ctx,#36]		@ is_base2_26
	ldmia	$ctx!,{$h0-$h4}		@ load hash value
	str	$len,[sp,#16]		@ offload stuff
	str	$ctx,[sp,#12]

	adds	$r0,$h0,$h1,lsl#26	@ base 2^26 -> base 2^32
	mov	$r1,$h1,lsr#6
	adcs	$r1,$r1,$h2,lsl#20
	mov	$r2,$h2,lsr#12
	adcs	$r2,$r2,$h3,lsl#14
	mov	$r3,$h3,lsr#18
	adcs	$r3,$r3,$h4,lsl#8
	mov	$len,#0
	teq	lr,#0
	str	$len,[$ctx,#16]		@ clear is_base2_26
	adc	$len,$len,$h4,lsr#24

	itttt	ne
	movne	$h0,$r0			@ choose between radixes
	movne	$h1,$r1
	movne	$h2,$r2
	movne	$h3,$r3
	ldmia	$ctx,{$r0-$r3}		@ load key
	it	ne
	movne	$h4,$len
#endif

	mov	lr,$inp
	cmp	$padbit,#0
	str	$r1,[sp,#20]
	str	$r2,[sp,#24]
	str	$r3,[sp,#28]
	b	.Loop

.align	4
.Loop:
#if __ARM_ARCH__<7
	ldrb	r0,[lr],#16		@ load input
# ifdef	__thumb2__
	it	hi
# endif
	addhi	$h4,$h4,#1		@ 1<<128
	ldrb	r1,[lr,#-15]
	ldrb	r2,[lr,#-14]
	ldrb	r3,[lr,#-13]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-12]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-11]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-10]
	adds	$h0,$h0,r3		@ accumulate input

	ldrb	r3,[lr,#-9]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-8]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-7]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-6]
	adcs	$h1,$h1,r3

	ldrb	r3,[lr,#-5]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-4]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-3]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-2]
	adcs	$h2,$h2,r3

	ldrb	r3,[lr,#-1]
	orr	r1,r0,r1,lsl#8
	str	lr,[sp,#8]		@ offload input pointer
	orr	r2,r1,r2,lsl#16
	add	$s1,$r1,$r1,lsr#2
	orr	r3,r2,r3,lsl#24
#else
	ldr	r0,[lr],#16		@ load input
	it	hi
	addhi	$h4,$h4,#1		@ padbit
	ldr	r1,[lr,#-12]
	ldr	r2,[lr,#-8]
	ldr	r3,[lr,#-4]
# ifdef	__ARMEB__
	rev	r0,r0
	rev	r1,r1
	rev	r2,r2
	rev	r3,r3
# endif
	adds	$h0,$h0,r0		@ accumulate input
	str	lr,[sp,#8]		@ offload input pointer
	adcs	$h1,$h1,r1
	add	$s1,$r1,$r1,lsr#2
	adcs	$h2,$h2,r2
#endif
	add	$s2,$r2,$r2,lsr#2
	adcs	$h3,$h3,r3
	add	$s3,$r3,$r3,lsr#2

	umull	r2,r3,$h1,$r0
	 adc	$h4,$h4,#0
	umull	r0,r1,$h0,$r0
	umlal	r2,r3,$h4,$s1
	umlal	r0,r1,$h3,$s1
	ldr	$r1,[sp,#20]		@ reload $r1
	umlal	r2,r3,$h2,$s3
	umlal	r0,r1,$h1,$s3
	umlal	r2,r3,$h3,$s2
	umlal	r0,r1,$h2,$s2
	umlal	r2,r3,$h0,$r1
	str	r0,[sp,#0]		@ future $h0
	 mul	r0,$s2,$h4
	ldr	$r2,[sp,#24]		@ reload $r2
	adds	r2,r2,r1		@ d1+=d0>>32
	 eor	r1,r1,r1
	adc	lr,r3,#0		@ future $h2
	str	r2,[sp,#4]		@ future $h1

	mul	r2,$s3,$h4
	eor	r3,r3,r3
	umlal	r0,r1,$h3,$s3
	ldr	$r3,[sp,#28]		@ reload $r3
	umlal	r2,r3,$h3,$r0
	umlal	r0,r1,$h2,$r0
	umlal	r2,r3,$h2,$r1
	umlal	r0,r1,$h1,$r1
	umlal	r2,r3,$h1,$r2
	umlal	r0,r1,$h0,$r2
	umlal	r2,r3,$h0,$r3
	ldr	$h0,[sp,#0]
	mul	$h4,$r0,$h4
	ldr	$h1,[sp,#4]

	adds	$h2,lr,r0		@ d2+=d1>>32
	ldr	lr,[sp,#8]		@ reload input pointer
	adc	r1,r1,#0
	adds	$h3,r2,r1		@ d3+=d2>>32
	ldr	r0,[sp,#16]		@ reload end pointer
	adc	r3,r3,#0
	add	$h4,$h4,r3		@ h4+=d3>>32

	and	r1,$h4,#-4
	and	$h4,$h4,#3
	add	r1,r1,r1,lsr#2		@ *=5
	adds	$h0,$h0,r1
	adcs	$h1,$h1,#0
	adcs	$h2,$h2,#0
	adcs	$h3,$h3,#0
	adc	$h4,$h4,#0

	cmp	r0,lr			@ done yet?
	bhi	.Loop

	ldr	$ctx,[sp,#12]
	add	sp,sp,#32
	stmdb	$ctx,{$h0-$h4}		@ store the result

.Lno_data:
#if	__ARM_ARCH__>=5
	ldmia	sp!,{r3-r11,pc}
#else
	ldmia	sp!,{r3-r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_blocks,.-poly1305_blocks
___
}
{
my ($ctx,$mac,$nonce)=map("r$_",(0..2));
my ($h0,$h1,$h2,$h3,$h4,$g0,$g1,$g2,$g3)=map("r$_",(3..11));
my $g4=$ctx;

$code.=<<___;
.type	poly1305_emit,%function
.align	5
poly1305_emit:
.Lpoly1305_emit:
	stmdb	sp!,{r4-r11}

	ldmia	$ctx,{$h0-$h4}

#if __ARM_ARCH__>=7
	ldr	ip,[$ctx,#36]		@ is_base2_26

	adds	$g0,$h0,$h1,lsl#26	@ base 2^26 -> base 2^32
	mov	$g1,$h1,lsr#6
	adcs	$g1,$g1,$h2,lsl#20
	mov	$g2,$h2,lsr#12
	adcs	$g2,$g2,$h3,lsl#14
	mov	$g3,$h3,lsr#18
	adcs	$g3,$g3,$h4,lsl#8
	mov	$g4,#0
	adc	$g4,$g4,$h4,lsr#24

	tst	ip,ip
	itttt	ne
	movne	$h0,$g0
	movne	$h1,$g1
	movne	$h2,$g2
	movne	$h3,$g3
	it	ne
	movne	$h4,$g4
#endif

	adds	$g0,$h0,#5		@ compare to modulus
	adcs	$g1,$h1,#0
	adcs	$g2,$h2,#0
	adcs	$g3,$h3,#0
	adc	$g4,$h4,#0
	tst	$g4,#4			@ did it carry/borrow?

#ifdef	__thumb2__
	it	ne
#endif
	movne	$h0,$g0
	ldr	$g0,[$nonce,#0]
#ifdef	__thumb2__
	it	ne
#endif
	movne	$h1,$g1
	ldr	$g1,[$nonce,#4]
#ifdef	__thumb2__
	it	ne
#endif
	movne	$h2,$g2
	ldr	$g2,[$nonce,#8]
#ifdef	__thumb2__
	it	ne
#endif
	movne	$h3,$g3
	ldr	$g3,[$nonce,#12]

	adds	$h0,$h0,$g0
	adcs	$h1,$h1,$g1
	adcs	$h2,$h2,$g2
	adc	$h3,$h3,$g3

#if __ARM_ARCH__>=7
# ifdef __ARMEB__
	rev	$h0,$h0
	rev	$h1,$h1
	rev	$h2,$h2
	rev	$h3,$h3
# endif
	str	$h0,[$mac,#0]
	str	$h1,[$mac,#4]
	str	$h2,[$mac,#8]
	str	$h3,[$mac,#12]
#else
	strb	$h0,[$mac,#0]
	mov	$h0,$h0,lsr#8
	strb	$h1,[$mac,#4]
	mov	$h1,$h1,lsr#8
	strb	$h2,[$mac,#8]
	mov	$h2,$h2,lsr#8
	strb	$h3,[$mac,#12]
	mov	$h3,$h3,lsr#8

	strb	$h0,[$mac,#1]
	mov	$h0,$h0,lsr#8
	strb	$h1,[$mac,#5]
	mov	$h1,$h1,lsr#8
	strb	$h2,[$mac,#9]
	mov	$h2,$h2,lsr#8
	strb	$h3,[$mac,#13]
	mov	$h3,$h3,lsr#8

	strb	$h0,[$mac,#2]
	mov	$h0,$h0,lsr#8
	strb	$h1,[$mac,#6]
	mov	$h1,$h1,lsr#8
	strb	$h2,[$mac,#10]
	mov	$h2,$h2,lsr#8
	strb	$h3,[$mac,#14]
	mov	$h3,$h3,lsr#8

	strb	$h0,[$mac,#3]
	strb	$h1,[$mac,#7]
	strb	$h2,[$mac,#11]
	strb	$h3,[$mac,#15]
#endif
	ldmia	sp!,{r4-r11}
#if	__ARM_ARCH__>=5
	ret				@ bx	lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_emit,.-poly1305_emit
___
{
my ($R0,$R1,$S1,$R2,$S2,$R3,$S3,$R4,$S4) = map("d$_",(0..9));
my ($D0,$D1,$D2,$D3,$D4, $H0,$H1,$H2,$H3,$H4) = map("q$_",(5..14));
my ($T0,$T1,$MASK) = map("q$_",(15,4,0));

my ($in2,$zeros,$tbl0,$tbl1) = map("r$_",(4..7));

$code.=<<___;
#if	__ARM_MAX_ARCH__>=7
.fpu	neon

.type	poly1305_init_neon,%function
.align	5
poly1305_init_neon:
.Lpoly1305_init_neon:
	ldr	r3,[$ctx,#48]		@ first table element
	cmp	r3,#-1			@ is value impossible?
	bne	.Lno_init_neon

	ldr	r4,[$ctx,#20]		@ load key base 2^32
	ldr	r5,[$ctx,#24]
	ldr	r6,[$ctx,#28]
	ldr	r7,[$ctx,#32]

	and	r2,r4,#0x03ffffff	@ base 2^32 -> base 2^26
	mov	r3,r4,lsr#26
	mov	r4,r5,lsr#20
	orr	r3,r3,r5,lsl#6
	mov	r5,r6,lsr#14
	orr	r4,r4,r6,lsl#12
	mov	r6,r7,lsr#8
	orr	r5,r5,r7,lsl#18
	and	r3,r3,#0x03ffffff
	and	r4,r4,#0x03ffffff
	and	r5,r5,#0x03ffffff

	vdup.32	$R0,r2			@ r^1 in both lanes
	add	r2,r3,r3,lsl#2		@ *5
	vdup.32	$R1,r3
	add	r3,r4,r4,lsl#2
	vdup.32	$S1,r2
	vdup.32	$R2,r4
	add	r4,r5,r5,lsl#2
	vdup.32	$S2,r3
	vdup.32	$R3,r5
	add	r5,r6,r6,lsl#2
	vdup.32	$S3,r4
	vdup.32	$R4,r6
	vdup.32	$S4,r5

	mov	$zeros,#2		@ counter

.Lsquare_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ d0 = h0*r0 + h4*5*r1 + h3*5*r2 + h2*5*r3 + h1*5*r4
	@ d1 = h1*r0 + h0*r1   + h4*5*r2 + h3*5*r3 + h2*5*r4
	@ d2 = h2*r0 + h1*r1   + h0*r2   + h4*5*r3 + h3*5*r4
	@ d3 = h3*r0 + h2*r1   + h1*r2   + h0*r3   + h4*5*r4
	@ d4 = h4*r0 + h3*r1   + h2*r2   + h1*r3   + h0*r4

	vmull.u32	$D0,$R0,${R0}[1]
	vmull.u32	$D1,$R1,${R0}[1]
	vmull.u32	$D2,$R2,${R0}[1]
	vmull.u32	$D3,$R3,${R0}[1]
	vmull.u32	$D4,$R4,${R0}[1]

	vmlal.u32	$D0,$R4,${S1}[1]
	vmlal.u32	$D1,$R0,${R1}[1]
	vmlal.u32	$D2,$R1,${R1}[1]
	vmlal.u32	$D3,$R2,${R1}[1]
	vmlal.u32	$D4,$R3,${R1}[1]

	vmlal.u32	$D0,$R3,${S2}[1]
	vmlal.u32	$D1,$R4,${S2}[1]
	vmlal.u32	$D3,$R1,${R2}[1]
	vmlal.u32	$D2,$R0,${R2}[1]
	vmlal.u32	$D4,$R2,${R2}[1]

	vmlal.u32	$D0,$R2,${S3}[1]
	vmlal.u32	$D3,$R0,${R3}[1]
	vmlal.u32	$D1,$R3,${S3}[1]
	vmlal.u32	$D2,$R4,${S3}[1]
	vmlal.u32	$D4,$R1,${R3}[1]

	vmlal.u32	$D3,$R4,${S4}[1]
	vmlal.u32	$D0,$R1,${S4}[1]
	vmlal.u32	$D1,$R2,${S4}[1]
	vmlal.u32	$D2,$R3,${S4}[1]
	vmlal.u32	$D4,$R0,${R4}[1]

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction as discussed in "NEON crypto" by D.J. Bernstein
	@ and P. Schwabe
	@
	@ H0>>+H1>>+H2>>+H3>>+H4
	@ H3>>+H4>>*5+H0>>+H1
	@
	@ Trivia.
	@
	@ Result of multiplication of n-bit number by m-bit number is
	@ n+m bits wide. However! Even though 2^n is a n+1-bit number,
	@ m-bit number multiplied by 2^n is still n+m bits wide.
	@
	@ Sum of two n-bit numbers is n+1 bits wide, sum of three - n+2,
	@ and so is sum of four. Sum of 2^m n-m-bit numbers and n-bit
	@ one is n+1 bits wide.
	@
	@ >>+ denotes Hnext += Hn>>26, Hn &= 0x3ffffff. This means that
	@ H0, H2, H3 are guaranteed to be 26 bits wide, while H1 and H4
	@ can be 27. However! In cases when their width exceeds 26 bits
	@ they are limited by 2^26+2^6. This in turn means that *sum*
	@ of the products with these values can still be viewed as sum
	@ of 52-bit numbers as long as the amount of addends is not a
	@ power of 2. For example,
	@
	@ H4 = H4*R0 + H3*R1 + H2*R2 + H1*R3 + H0 * R4,
	@
	@ which can't be larger than 5 * (2^26 + 2^6) * (2^26 + 2^6), or
	@ 5 * (2^52 + 2*2^32 + 2^12), which in turn is smaller than
	@ 8 * (2^52) or 2^55. However, the value is then multiplied by
	@ by 5, so we should be looking at 5 * 5 * (2^52 + 2^33 + 2^12),
	@ which is less than 32 * (2^52) or 2^57. And when processing
	@ data we are looking at triple as many addends...
	@
	@ In key setup procedure pre-reduced H0 is limited by 5*4+1 and
	@ 5*H4 - by 5*5 52-bit addends, or 57 bits. But when hashing the
	@ input H0 is limited by (5*4+1)*3 addends, or 58 bits, while
	@ 5*H4 by 5*5*3, or 59[!] bits. How is this relevant? vmlal.u32
	@ instruction accepts 2x32-bit input and writes 2x64-bit result.
	@ This means that result of reduction have to be compressed upon
	@ loop wrap-around. This can be done in the process of reduction
	@ to minimize amount of instructions [as well as amount of
	@ 128-bit instructions, which benefits low-end processors], but
	@ one has to watch for H2 (which is narrower than H0) and 5*H4
	@ not being wider than 58 bits, so that result of right shift
	@ by 26 bits fits in 32 bits. This is also useful on x86,
	@ because it allows to use paddd in place for paddq, which
	@ benefits Atom, where paddq is ridiculously slow.

	vshr.u64	$T0,$D3,#26
	vmovn.i64	$D3#lo,$D3
	 vshr.u64	$T1,$D0,#26
	 vmovn.i64	$D0#lo,$D0
	vadd.i64	$D4,$D4,$T0		@ h3 -> h4
	vbic.i32	$D3#lo,#0xfc000000	@ &=0x03ffffff
	 vadd.i64	$D1,$D1,$T1		@ h0 -> h1
	 vbic.i32	$D0#lo,#0xfc000000

	vshrn.u64	$T0#lo,$D4,#26
	vmovn.i64	$D4#lo,$D4
	 vshr.u64	$T1,$D1,#26
	 vmovn.i64	$D1#lo,$D1
	 vadd.i64	$D2,$D2,$T1		@ h1 -> h2
	vbic.i32	$D4#lo,#0xfc000000
	 vbic.i32	$D1#lo,#0xfc000000

	vadd.i32	$D0#lo,$D0#lo,$T0#lo
	vshl.u32	$T0#lo,$T0#lo,#2
	 vshrn.u64	$T1#lo,$D2,#26
	 vmovn.i64	$D2#lo,$D2
	vadd.i32	$D0#lo,$D0#lo,$T0#lo	@ h4 -> h0
	 vadd.i32	$D3#lo,$D3#lo,$T1#lo	@ h2 -> h3
	 vbic.i32	$D2#lo,#0xfc000000

	vshr.u32	$T0#lo,$D0#lo,#26
	vbic.i32	$D0#lo,#0xfc000000
	 vshr.u32	$T1#lo,$D3#lo,#26
	 vbic.i32	$D3#lo,#0xfc000000
	vadd.i32	$D1#lo,$D1#lo,$T0#lo	@ h0 -> h1
	 vadd.i32	$D4#lo,$D4#lo,$T1#lo	@ h3 -> h4

	subs		$zeros,$zeros,#1
	beq		.Lsquare_break_neon

	add		$tbl0,$ctx,#(48+0*9*4)
	add		$tbl1,$ctx,#(48+1*9*4)

	vtrn.32		$R0,$D0#lo		@ r^2:r^1
	vtrn.32		$R2,$D2#lo
	vtrn.32		$R3,$D3#lo
	vtrn.32		$R1,$D1#lo
	vtrn.32		$R4,$D4#lo

	vshl.u32	$S2,$R2,#2		@ *5
	vshl.u32	$S3,$R3,#2
	vshl.u32	$S1,$R1,#2
	vshl.u32	$S4,$R4,#2
	vadd.i32	$S2,$S2,$R2
	vadd.i32	$S1,$S1,$R1
	vadd.i32	$S3,$S3,$R3
	vadd.i32	$S4,$S4,$R4

	vst4.32		{${R0}[0],${R1}[0],${S1}[0],${R2}[0]},[$tbl0]!
	vst4.32		{${R0}[1],${R1}[1],${S1}[1],${R2}[1]},[$tbl1]!
	vst4.32		{${S2}[0],${R3}[0],${S3}[0],${R4}[0]},[$tbl0]!
	vst4.32		{${S2}[1],${R3}[1],${S3}[1],${R4}[1]},[$tbl1]!
	vst1.32		{${S4}[0]},[$tbl0,:32]
	vst1.32		{${S4}[1]},[$tbl1,:32]

	b		.Lsquare_neon

.align	4
.Lsquare_break_neon:
	add		$tbl0,$ctx,#(48+2*4*9)
	add		$tbl1,$ctx,#(48+3*4*9)

	vmov		$R0,$D0#lo		@ r^4:r^3
	vshl.u32	$S1,$D1#lo,#2		@ *5
	vmov		$R1,$D1#lo
	vshl.u32	$S2,$D2#lo,#2
	vmov		$R2,$D2#lo
	vshl.u32	$S3,$D3#lo,#2
	vmov		$R3,$D3#lo
	vshl.u32	$S4,$D4#lo,#2
	vmov		$R4,$D4#lo
	vadd.i32	$S1,$S1,$D1#lo
	vadd.i32	$S2,$S2,$D2#lo
	vadd.i32	$S3,$S3,$D3#lo
	vadd.i32	$S4,$S4,$D4#lo

	vst4.32		{${R0}[0],${R1}[0],${S1}[0],${R2}[0]},[$tbl0]!
	vst4.32		{${R0}[1],${R1}[1],${S1}[1],${R2}[1]},[$tbl1]!
	vst4.32		{${S2}[0],${R3}[0],${S3}[0],${R4}[0]},[$tbl0]!
	vst4.32		{${S2}[1],${R3}[1],${S3}[1],${R4}[1]},[$tbl1]!
	vst1.32		{${S4}[0]},[$tbl0]
	vst1.32		{${S4}[1]},[$tbl1]

.Lno_init_neon:
	ret				@ bx	lr
.size	poly1305_init_neon,.-poly1305_init_neon

.globl	poly1305_blocks_neon
.type	poly1305_blocks_neon,%function
.align	5
poly1305_blocks_neon:
.Lpoly1305_blocks_neon:
	ldr	ip,[$ctx,#36]		@ is_base2_26

	cmp	$len,#64
	blo	.Lpoly1305_blocks

	stmdb	sp!,{r4-r7}
	vstmdb	sp!,{d8-d15}		@ ABI specification says so

	tst	ip,ip			@ is_base2_26?
	bne	.Lbase2_26_neon

	stmdb	sp!,{r1-r3,lr}
	bl	.Lpoly1305_init_neon

	ldr	r4,[$ctx,#0]		@ load hash value base 2^32
	ldr	r5,[$ctx,#4]
	ldr	r6,[$ctx,#8]
	ldr	r7,[$ctx,#12]
	ldr	ip,[$ctx,#16]

	and	r2,r4,#0x03ffffff	@ base 2^32 -> base 2^26
	mov	r3,r4,lsr#26
	 veor	$D0#lo,$D0#lo,$D0#lo
	mov	r4,r5,lsr#20
	orr	r3,r3,r5,lsl#6
	 veor	$D1#lo,$D1#lo,$D1#lo
	mov	r5,r6,lsr#14
	orr	r4,r4,r6,lsl#12
	 veor	$D2#lo,$D2#lo,$D2#lo
	mov	r6,r7,lsr#8
	orr	r5,r5,r7,lsl#18
	 veor	$D3#lo,$D3#lo,$D3#lo
	and	r3,r3,#0x03ffffff
	orr	r6,r6,ip,lsl#24
	 veor	$D4#lo,$D4#lo,$D4#lo
	and	r4,r4,#0x03ffffff
	mov	r1,#1
	and	r5,r5,#0x03ffffff
	str	r1,[$ctx,#36]		@ set is_base2_26

	vmov.32	$D0#lo[0],r2
	vmov.32	$D1#lo[0],r3
	vmov.32	$D2#lo[0],r4
	vmov.32	$D3#lo[0],r5
	vmov.32	$D4#lo[0],r6
	adr	$zeros,.Lzeros

	ldmia	sp!,{r1-r3,lr}
	b	.Lhash_loaded

.align	4
.Lbase2_26_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ load hash value

	veor		$D0#lo,$D0#lo,$D0#lo
	veor		$D1#lo,$D1#lo,$D1#lo
	veor		$D2#lo,$D2#lo,$D2#lo
	veor		$D3#lo,$D3#lo,$D3#lo
	veor		$D4#lo,$D4#lo,$D4#lo
	vld4.32		{$D0#lo[0],$D1#lo[0],$D2#lo[0],$D3#lo[0]},[$ctx]!
	adr		$zeros,.Lzeros
	vld1.32		{$D4#lo[0]},[$ctx]
	sub		$ctx,$ctx,#16		@ rewind

.Lhash_loaded:
	add		$in2,$inp,#32
	mov		$padbit,$padbit,lsl#24
	tst		$len,#31
	beq		.Leven

	vld4.32		{$H0#lo[0],$H1#lo[0],$H2#lo[0],$H3#lo[0]},[$inp]!
	vmov.32		$H4#lo[0],$padbit
	sub		$len,$len,#16
	add		$in2,$inp,#32

# ifdef	__ARMEB__
	vrev32.8	$H0,$H0
	vrev32.8	$H3,$H3
	vrev32.8	$H1,$H1
	vrev32.8	$H2,$H2
# endif
	vsri.u32	$H4#lo,$H3#lo,#8	@ base 2^32 -> base 2^26
	vshl.u32	$H3#lo,$H3#lo,#18

	vsri.u32	$H3#lo,$H2#lo,#14
	vshl.u32	$H2#lo,$H2#lo,#12
	vadd.i32	$H4#hi,$H4#lo,$D4#lo	@ add hash value and move to #hi

	vbic.i32	$H3#lo,#0xfc000000
	vsri.u32	$H2#lo,$H1#lo,#20
	vshl.u32	$H1#lo,$H1#lo,#6

	vbic.i32	$H2#lo,#0xfc000000
	vsri.u32	$H1#lo,$H0#lo,#26
	vadd.i32	$H3#hi,$H3#lo,$D3#lo

	vbic.i32	$H0#lo,#0xfc000000
	vbic.i32	$H1#lo,#0xfc000000
	vadd.i32	$H2#hi,$H2#lo,$D2#lo

	vadd.i32	$H0#hi,$H0#lo,$D0#lo
	vadd.i32	$H1#hi,$H1#lo,$D1#lo

	mov		$tbl1,$zeros
	add		$tbl0,$ctx,#48

	cmp		$len,$len
	b		.Long_tail

.align	4
.Leven:
	subs		$len,$len,#64
	it		lo
	movlo		$in2,$zeros

	vmov.i32	$H4,#1<<24		@ padbit, yes, always
	vld4.32		{$H0#lo,$H1#lo,$H2#lo,$H3#lo},[$inp]	@ inp[0:1]
	add		$inp,$inp,#64
	vld4.32		{$H0#hi,$H1#hi,$H2#hi,$H3#hi},[$in2]	@ inp[2:3] (or 0)
	add		$in2,$in2,#64
	itt		hi
	addhi		$tbl1,$ctx,#(48+1*9*4)
	addhi		$tbl0,$ctx,#(48+3*9*4)

# ifdef	__ARMEB__
	vrev32.8	$H0,$H0
	vrev32.8	$H3,$H3
	vrev32.8	$H1,$H1
	vrev32.8	$H2,$H2
# endif
	vsri.u32	$H4,$H3,#8		@ base 2^32 -> base 2^26
	vshl.u32	$H3,$H3,#18

	vsri.u32	$H3,$H2,#14
	vshl.u32	$H2,$H2,#12

	vbic.i32	$H3,#0xfc000000
	vsri.u32	$H2,$H1,#20
	vshl.u32	$H1,$H1,#6

	vbic.i32	$H2,#0xfc000000
	vsri.u32	$H1,$H0,#26

	vbic.i32	$H0,#0xfc000000
	vbic.i32	$H1,#0xfc000000

	bls		.Lskip_loop

	vld4.32		{${R0}[1],${R1}[1],${S1}[1],${R2}[1]},[$tbl1]!	@ load r^2
	vld4.32		{${R0}[0],${R1}[0],${S1}[0],${R2}[0]},[$tbl0]!	@ load r^4
	vld4.32		{${S2}[1],${R3}[1],${S3}[1],${R4}[1]},[$tbl1]!
	vld4.32		{${S2}[0],${R3}[0],${S3}[0],${R4}[0]},[$tbl0]!
	b		.Loop_neon

.align	5
.Loop_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2
	@ ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^3+inp[7]*r
	@   \___________________/
	@ ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2+inp[8])*r^2
	@ ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^4+inp[7]*r^2+inp[9])*r
	@   \___________________/ \____________________/
	@
	@ Note that we start with inp[2:3]*r^2. This is because it
	@ doesn't depend on reduction in previous iteration.
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ d4 = h4*r0 + h3*r1   + h2*r2   + h1*r3   + h0*r4
	@ d3 = h3*r0 + h2*r1   + h1*r2   + h0*r3   + h4*5*r4
	@ d2 = h2*r0 + h1*r1   + h0*r2   + h4*5*r3 + h3*5*r4
	@ d1 = h1*r0 + h0*r1   + h4*5*r2 + h3*5*r3 + h2*5*r4
	@ d0 = h0*r0 + h4*5*r1 + h3*5*r2 + h2*5*r3 + h1*5*r4

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ inp[2:3]*r^2

	vadd.i32	$H2#lo,$H2#lo,$D2#lo	@ accumulate inp[0:1]
	vmull.u32	$D2,$H2#hi,${R0}[1]
	vadd.i32	$H0#lo,$H0#lo,$D0#lo
	vmull.u32	$D0,$H0#hi,${R0}[1]
	vadd.i32	$H3#lo,$H3#lo,$D3#lo
	vmull.u32	$D3,$H3#hi,${R0}[1]
	vmlal.u32	$D2,$H1#hi,${R1}[1]
	vadd.i32	$H1#lo,$H1#lo,$D1#lo
	vmull.u32	$D1,$H1#hi,${R0}[1]

	vadd.i32	$H4#lo,$H4#lo,$D4#lo
	vmull.u32	$D4,$H4#hi,${R0}[1]
	subs		$len,$len,#64
	vmlal.u32	$D0,$H4#hi,${S1}[1]
	it		lo
	movlo		$in2,$zeros
	vmlal.u32	$D3,$H2#hi,${R1}[1]
	vld1.32		${S4}[1],[$tbl1,:32]
	vmlal.u32	$D1,$H0#hi,${R1}[1]
	vmlal.u32	$D4,$H3#hi,${R1}[1]

	vmlal.u32	$D0,$H3#hi,${S2}[1]
	vmlal.u32	$D3,$H1#hi,${R2}[1]
	vmlal.u32	$D4,$H2#hi,${R2}[1]
	vmlal.u32	$D1,$H4#hi,${S2}[1]
	vmlal.u32	$D2,$H0#hi,${R2}[1]

	vmlal.u32	$D3,$H0#hi,${R3}[1]
	vmlal.u32	$D0,$H2#hi,${S3}[1]
	vmlal.u32	$D4,$H1#hi,${R3}[1]
	vmlal.u32	$D1,$H3#hi,${S3}[1]
	vmlal.u32	$D2,$H4#hi,${S3}[1]

	vmlal.u32	$D3,$H4#hi,${S4}[1]
	vmlal.u32	$D0,$H1#hi,${S4}[1]
	vmlal.u32	$D4,$H0#hi,${R4}[1]
	vmlal.u32	$D1,$H2#hi,${S4}[1]
	vmlal.u32	$D2,$H3#hi,${S4}[1]

	vld4.32		{$H0#hi,$H1#hi,$H2#hi,$H3#hi},[$in2]	@ inp[2:3] (or 0)
	add		$in2,$in2,#64

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ (hash+inp[0:1])*r^4 and accumulate

	vmlal.u32	$D3,$H3#lo,${R0}[0]
	vmlal.u32	$D0,$H0#lo,${R0}[0]
	vmlal.u32	$D4,$H4#lo,${R0}[0]
	vmlal.u32	$D1,$H1#lo,${R0}[0]
	vmlal.u32	$D2,$H2#lo,${R0}[0]
	vld1.32		${S4}[0],[$tbl0,:32]

	vmlal.u32	$D3,$H2#lo,${R1}[0]
	vmlal.u32	$D0,$H4#lo,${S1}[0]
	vmlal.u32	$D4,$H3#lo,${R1}[0]
	vmlal.u32	$D1,$H0#lo,${R1}[0]
	vmlal.u32	$D2,$H1#lo,${R1}[0]

	vmlal.u32	$D3,$H1#lo,${R2}[0]
	vmlal.u32	$D0,$H3#lo,${S2}[0]
	vmlal.u32	$D4,$H2#lo,${R2}[0]
	vmlal.u32	$D1,$H4#lo,${S2}[0]
	vmlal.u32	$D2,$H0#lo,${R2}[0]

	vmlal.u32	$D3,$H0#lo,${R3}[0]
	vmlal.u32	$D0,$H2#lo,${S3}[0]
	vmlal.u32	$D4,$H1#lo,${R3}[0]
	vmlal.u32	$D1,$H3#lo,${S3}[0]
	vmlal.u32	$D3,$H4#lo,${S4}[0]

	vmlal.u32	$D2,$H4#lo,${S3}[0]
	vmlal.u32	$D0,$H1#lo,${S4}[0]
	vmlal.u32	$D4,$H0#lo,${R4}[0]
	vmov.i32	$H4,#1<<24		@ padbit, yes, always
	vmlal.u32	$D1,$H2#lo,${S4}[0]
	vmlal.u32	$D2,$H3#lo,${S4}[0]

	vld4.32		{$H0#lo,$H1#lo,$H2#lo,$H3#lo},[$inp]	@ inp[0:1]
	add		$inp,$inp,#64
# ifdef	__ARMEB__
	vrev32.8	$H0,$H0
	vrev32.8	$H1,$H1
	vrev32.8	$H2,$H2
	vrev32.8	$H3,$H3
# endif

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction interleaved with base 2^32 -> base 2^26 of
	@ inp[0:3] previously loaded to $H0-$H3 and smashed to $H0-$H4.

	vshr.u64	$T0,$D3,#26
	vmovn.i64	$D3#lo,$D3
	 vshr.u64	$T1,$D0,#26
	 vmovn.i64	$D0#lo,$D0
	vadd.i64	$D4,$D4,$T0		@ h3 -> h4
	vbic.i32	$D3#lo,#0xfc000000
	  vsri.u32	$H4,$H3,#8		@ base 2^32 -> base 2^26
	 vadd.i64	$D1,$D1,$T1		@ h0 -> h1
	  vshl.u32	$H3,$H3,#18
	 vbic.i32	$D0#lo,#0xfc000000

	vshrn.u64	$T0#lo,$D4,#26
	vmovn.i64	$D4#lo,$D4
	 vshr.u64	$T1,$D1,#26
	 vmovn.i64	$D1#lo,$D1
	 vadd.i64	$D2,$D2,$T1		@ h1 -> h2
	  vsri.u32	$H3,$H2,#14
	vbic.i32	$D4#lo,#0xfc000000
	  vshl.u32	$H2,$H2,#12
	 vbic.i32	$D1#lo,#0xfc000000

	vadd.i32	$D0#lo,$D0#lo,$T0#lo
	vshl.u32	$T0#lo,$T0#lo,#2
	  vbic.i32	$H3,#0xfc000000
	 vshrn.u64	$T1#lo,$D2,#26
	 vmovn.i64	$D2#lo,$D2
	vaddl.u32	$D0,$D0#lo,$T0#lo	@ h4 -> h0 [widen for a sec]
	  vsri.u32	$H2,$H1,#20
	 vadd.i32	$D3#lo,$D3#lo,$T1#lo	@ h2 -> h3
	  vshl.u32	$H1,$H1,#6
	 vbic.i32	$D2#lo,#0xfc000000
	  vbic.i32	$H2,#0xfc000000

	vshrn.u64	$T0#lo,$D0,#26		@ re-narrow
	vmovn.i64	$D0#lo,$D0
	  vsri.u32	$H1,$H0,#26
	  vbic.i32	$H0,#0xfc000000
	 vshr.u32	$T1#lo,$D3#lo,#26
	 vbic.i32	$D3#lo,#0xfc000000
	vbic.i32	$D0#lo,#0xfc000000
	vadd.i32	$D1#lo,$D1#lo,$T0#lo	@ h0 -> h1
	 vadd.i32	$D4#lo,$D4#lo,$T1#lo	@ h3 -> h4
	  vbic.i32	$H1,#0xfc000000

	bhi		.Loop_neon

.Lskip_loop:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ multiply (inp[0:1]+hash) or inp[2:3] by r^2:r^1

	add		$tbl1,$ctx,#(48+0*9*4)
	add		$tbl0,$ctx,#(48+1*9*4)
	adds		$len,$len,#32
	it		ne
	movne		$len,#0
	bne		.Long_tail

	vadd.i32	$H2#hi,$H2#lo,$D2#lo	@ add hash value and move to #hi
	vadd.i32	$H0#hi,$H0#lo,$D0#lo
	vadd.i32	$H3#hi,$H3#lo,$D3#lo
	vadd.i32	$H1#hi,$H1#lo,$D1#lo
	vadd.i32	$H4#hi,$H4#lo,$D4#lo

.Long_tail:
	vld4.32		{${R0}[1],${R1}[1],${S1}[1],${R2}[1]},[$tbl1]!	@ load r^1
	vld4.32		{${R0}[0],${R1}[0],${S1}[0],${R2}[0]},[$tbl0]!	@ load r^2

	vadd.i32	$H2#lo,$H2#lo,$D2#lo	@ can be redundant
	vmull.u32	$D2,$H2#hi,$R0
	vadd.i32	$H0#lo,$H0#lo,$D0#lo
	vmull.u32	$D0,$H0#hi,$R0
	vadd.i32	$H3#lo,$H3#lo,$D3#lo
	vmull.u32	$D3,$H3#hi,$R0
	vadd.i32	$H1#lo,$H1#lo,$D1#lo
	vmull.u32	$D1,$H1#hi,$R0
	vadd.i32	$H4#lo,$H4#lo,$D4#lo
	vmull.u32	$D4,$H4#hi,$R0

	vmlal.u32	$D0,$H4#hi,$S1
	vld4.32		{${S2}[1],${R3}[1],${S3}[1],${R4}[1]},[$tbl1]!
	vmlal.u32	$D3,$H2#hi,$R1
	vld4.32		{${S2}[0],${R3}[0],${S3}[0],${R4}[0]},[$tbl0]!
	vmlal.u32	$D1,$H0#hi,$R1
	vmlal.u32	$D4,$H3#hi,$R1
	vmlal.u32	$D2,$H1#hi,$R1

	vmlal.u32	$D3,$H1#hi,$R2
	vld1.32		${S4}[1],[$tbl1,:32]
	vmlal.u32	$D0,$H3#hi,$S2
	vld1.32		${S4}[0],[$tbl0,:32]
	vmlal.u32	$D4,$H2#hi,$R2
	vmlal.u32	$D1,$H4#hi,$S2
	vmlal.u32	$D2,$H0#hi,$R2

	vmlal.u32	$D3,$H0#hi,$R3
	 it		ne
	 addne		$tbl1,$ctx,#(48+2*9*4)
	vmlal.u32	$D0,$H2#hi,$S3
	 it		ne
	 addne		$tbl0,$ctx,#(48+3*9*4)
	vmlal.u32	$D4,$H1#hi,$R3
	vmlal.u32	$D1,$H3#hi,$S3
	vmlal.u32	$D2,$H4#hi,$S3

	vmlal.u32	$D3,$H4#hi,$S4
	 vorn		$MASK,$MASK,$MASK	@ all-ones, can be redundant
	vmlal.u32	$D0,$H1#hi,$S4
	 vshr.u64	$MASK,$MASK,#38
	vmlal.u32	$D4,$H0#hi,$R4
	vmlal.u32	$D1,$H2#hi,$S4
	vmlal.u32	$D2,$H3#hi,$S4

	beq		.Lshort_tail

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ (hash+inp[0:1])*r^4:r^3 and accumulate

	vld4.32		{${R0}[1],${R1}[1],${S1}[1],${R2}[1]},[$tbl1]!	@ load r^3
	vld4.32		{${R0}[0],${R1}[0],${S1}[0],${R2}[0]},[$tbl0]!	@ load r^4

	vmlal.u32	$D2,$H2#lo,$R0
	vmlal.u32	$D0,$H0#lo,$R0
	vmlal.u32	$D3,$H3#lo,$R0
	vmlal.u32	$D1,$H1#lo,$R0
	vmlal.u32	$D4,$H4#lo,$R0

	vmlal.u32	$D0,$H4#lo,$S1
	vld4.32		{${S2}[1],${R3}[1],${S3}[1],${R4}[1]},[$tbl1]!
	vmlal.u32	$D3,$H2#lo,$R1
	vld4.32		{${S2}[0],${R3}[0],${S3}[0],${R4}[0]},[$tbl0]!
	vmlal.u32	$D1,$H0#lo,$R1
	vmlal.u32	$D4,$H3#lo,$R1
	vmlal.u32	$D2,$H1#lo,$R1

	vmlal.u32	$D3,$H1#lo,$R2
	vld1.32		${S4}[1],[$tbl1,:32]
	vmlal.u32	$D0,$H3#lo,$S2
	vld1.32		${S4}[0],[$tbl0,:32]
	vmlal.u32	$D4,$H2#lo,$R2
	vmlal.u32	$D1,$H4#lo,$S2
	vmlal.u32	$D2,$H0#lo,$R2

	vmlal.u32	$D3,$H0#lo,$R3
	vmlal.u32	$D0,$H2#lo,$S3
	vmlal.u32	$D4,$H1#lo,$R3
	vmlal.u32	$D1,$H3#lo,$S3
	vmlal.u32	$D2,$H4#lo,$S3

	vmlal.u32	$D3,$H4#lo,$S4
	 vorn		$MASK,$MASK,$MASK	@ all-ones
	vmlal.u32	$D0,$H1#lo,$S4
	 vshr.u64	$MASK,$MASK,#38
	vmlal.u32	$D4,$H0#lo,$R4
	vmlal.u32	$D1,$H2#lo,$S4
	vmlal.u32	$D2,$H3#lo,$S4

.Lshort_tail:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ horizontal addition

	vadd.i64	$D3#lo,$D3#lo,$D3#hi
	vadd.i64	$D0#lo,$D0#lo,$D0#hi
	vadd.i64	$D4#lo,$D4#lo,$D4#hi
	vadd.i64	$D1#lo,$D1#lo,$D1#hi
	vadd.i64	$D2#lo,$D2#lo,$D2#hi

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction, but without narrowing

	vshr.u64	$T0,$D3,#26
	vand.i64	$D3,$D3,$MASK
	 vshr.u64	$T1,$D0,#26
	 vand.i64	$D0,$D0,$MASK
	vadd.i64	$D4,$D4,$T0		@ h3 -> h4
	 vadd.i64	$D1,$D1,$T1		@ h0 -> h1

	vshr.u64	$T0,$D4,#26
	vand.i64	$D4,$D4,$MASK
	 vshr.u64	$T1,$D1,#26
	 vand.i64	$D1,$D1,$MASK
	 vadd.i64	$D2,$D2,$T1		@ h1 -> h2

	vadd.i64	$D0,$D0,$T0
	vshl.u64	$T0,$T0,#2
	 vshr.u64	$T1,$D2,#26
	 vand.i64	$D2,$D2,$MASK
	vadd.i64	$D0,$D0,$T0		@ h4 -> h0
	 vadd.i64	$D3,$D3,$T1		@ h2 -> h3

	vshr.u64	$T0,$D0,#26
	vand.i64	$D0,$D0,$MASK
	 vshr.u64	$T1,$D3,#26
	 vand.i64	$D3,$D3,$MASK
	vadd.i64	$D1,$D1,$T0		@ h0 -> h1
	 vadd.i64	$D4,$D4,$T1		@ h3 -> h4

	cmp		$len,#0
	bne		.Leven

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ store hash value

	vst4.32		{$D0#lo[0],$D1#lo[0],$D2#lo[0],$D3#lo[0]},[$ctx]!
	vst1.32		{$D4#lo[0]},[$ctx]

	vldmia	sp!,{d8-d15}			@ epilogue
	ldmia	sp!,{r4-r7}
	ret					@ bx	lr
.size	poly1305_blocks_neon,.-poly1305_blocks_neon

.align	5
.Lzeros:
.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
#ifndef	__KERNEL__
.LOPENSSL_armcap:
# ifdef	_WIN32
.word	OPENSSL_armcap_P
# else
.word	OPENSSL_armcap_P-.Lpoly1305_init
# endif
.comm	OPENSSL_armcap_P,4,4
.hidden	OPENSSL_armcap_P
#endif
#endif
___
}	}
$code.=<<___;
.asciz	"Poly1305 for ARMv4/NEON, CRYPTOGAMS by \@dot-asm"
.align	2
___

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\bq([0-9]+)#(lo|hi)/sprintf "d%d",2*$1+($2 eq "hi")/geo	or
	s/\bret\b/bx	lr/go						or
	s/\bbx\s+lr\b/.word\t0xe12fff1e/go;	# make it possible to compile with -march=armv4

	print $_,"\n";
}
close STDOUT; # enforce flush
