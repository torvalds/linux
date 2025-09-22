#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# On 21264 RSA sign performance improves by 70/35/20/15 percent for
# 512/1024/2048/4096 bit key lengths. This is against vendor compiler
# instructed to '-tune host' code with in-line assembler. Other
# benchmarks improve by 15-20%. To anchor it to something else, the
# code provides approximately the same performance per GHz as AMD64.
# I.e. if you compare 1GHz 21264 and 2GHz Opteron, you'll observe ~2x
# difference.

# int bn_mul_mont(
$rp="a0";	# BN_ULONG *rp,
$ap="a1";	# const BN_ULONG *ap,
$bp="a2";	# const BN_ULONG *bp,
$np="a3";	# const BN_ULONG *np,
$n0="a4";	# const BN_ULONG *n0,
$num="a5";	# int num);

$lo0="t0";
$hi0="t1";
$lo1="t2";
$hi1="t3";
$aj="t4";
$bi="t5";
$nj="t6";
$tp="t7";
$alo="t8";
$ahi="t9";
$nlo="t10";
$nhi="t11";
$tj="t12";
$i="s3";
$j="s4";
$m1="s5";

$code=<<___;
#include <machine/asm.h>

.text

.set	noat
.set	noreorder

.globl	bn_mul_mont
.align	5
.ent	bn_mul_mont
bn_mul_mont:
	lda	sp,-48(sp)
	stq	ra,0(sp)
	stq	s3,8(sp)
	stq	s4,16(sp)
	stq	s5,24(sp)
	stq	fp,32(sp)
	mov	sp,fp
	.mask	0x0400f000,-48
	.frame	fp,48,ra
	.prologue 0

	.align	4
	.set	reorder
	sextl	$num,$num
	mov	0,v0
	cmplt	$num,4,AT
	bne	AT,.Lexit

	ldq	$hi0,0($ap)	# ap[0]
	s8addq	$num,16,AT
	ldq	$aj,8($ap)
	subq	sp,AT,sp
	ldq	$bi,0($bp)	# bp[0]
	lda	AT,-4096(zero)	# mov	-4096,AT
	ldq	$n0,0($n0)
	and	sp,AT,sp

	mulq	$hi0,$bi,$lo0
	ldq	$hi1,0($np)	# np[0]
	umulh	$hi0,$bi,$hi0
	ldq	$nj,8($np)

	mulq	$lo0,$n0,$m1

	mulq	$hi1,$m1,$lo1
	umulh	$hi1,$m1,$hi1

	addq	$lo1,$lo0,$lo1
	cmpult	$lo1,$lo0,AT
	addq	$hi1,AT,$hi1

	mulq	$aj,$bi,$alo
	mov	2,$j
	umulh	$aj,$bi,$ahi
	mov	sp,$tp

	mulq	$nj,$m1,$nlo
	s8addq	$j,$ap,$aj
	umulh	$nj,$m1,$nhi
	s8addq	$j,$np,$nj
.align	4
.L1st:
	.set	noreorder
	ldq	$aj,0($aj)
	addl	$j,1,$j
	ldq	$nj,0($nj)
	lda	$tp,8($tp)

	addq	$alo,$hi0,$lo0
	mulq	$aj,$bi,$alo
	cmpult	$lo0,$hi0,AT
	addq	$nlo,$hi1,$lo1

	mulq	$nj,$m1,$nlo
	addq	$ahi,AT,$hi0
	cmpult	$lo1,$hi1,v0
	cmplt	$j,$num,$tj

	umulh	$aj,$bi,$ahi
	addq	$nhi,v0,$hi1
	addq	$lo1,$lo0,$lo1
	s8addq	$j,$ap,$aj

	umulh	$nj,$m1,$nhi
	cmpult	$lo1,$lo0,v0
	addq	$hi1,v0,$hi1
	s8addq	$j,$np,$nj

	stq	$lo1,-8($tp)
	nop
	unop
	bne	$tj,.L1st
	.set	reorder

	addq	$alo,$hi0,$lo0
	addq	$nlo,$hi1,$lo1
	cmpult	$lo0,$hi0,AT
	cmpult	$lo1,$hi1,v0
	addq	$ahi,AT,$hi0
	addq	$nhi,v0,$hi1

	addq	$lo1,$lo0,$lo1
	cmpult	$lo1,$lo0,v0
	addq	$hi1,v0,$hi1

	stq	$lo1,0($tp)

	addq	$hi1,$hi0,$hi1
	cmpult	$hi1,$hi0,AT
	stq	$hi1,8($tp)
	stq	AT,16($tp)

	mov	1,$i
.align	4
.Louter:
	s8addq	$i,$bp,$bi
	ldq	$hi0,0($ap)
	ldq	$aj,8($ap)
	ldq	$bi,0($bi)
	ldq	$hi1,0($np)
	ldq	$nj,8($np)
	ldq	$tj,0(sp)

	mulq	$hi0,$bi,$lo0
	umulh	$hi0,$bi,$hi0

	addq	$lo0,$tj,$lo0
	cmpult	$lo0,$tj,AT
	addq	$hi0,AT,$hi0

	mulq	$lo0,$n0,$m1

	mulq	$hi1,$m1,$lo1
	umulh	$hi1,$m1,$hi1

	addq	$lo1,$lo0,$lo1
	cmpult	$lo1,$lo0,AT
	mov	2,$j
	addq	$hi1,AT,$hi1

	mulq	$aj,$bi,$alo
	mov	sp,$tp
	umulh	$aj,$bi,$ahi

	mulq	$nj,$m1,$nlo
	s8addq	$j,$ap,$aj
	umulh	$nj,$m1,$nhi
.align	4
.Linner:
	.set	noreorder
	ldq	$tj,8($tp)	#L0
	nop			#U1
	ldq	$aj,0($aj)	#L1
	s8addq	$j,$np,$nj	#U0

	ldq	$nj,0($nj)	#L0
	nop			#U1
	addq	$alo,$hi0,$lo0	#L1
	lda	$tp,8($tp)

	mulq	$aj,$bi,$alo	#U1
	cmpult	$lo0,$hi0,AT	#L0
	addq	$nlo,$hi1,$lo1	#L1
	addl	$j,1,$j

	mulq	$nj,$m1,$nlo	#U1
	addq	$ahi,AT,$hi0	#L0
	addq	$lo0,$tj,$lo0	#L1
	cmpult	$lo1,$hi1,v0	#U0

	umulh	$aj,$bi,$ahi	#U1
	cmpult	$lo0,$tj,AT	#L0
	addq	$lo1,$lo0,$lo1	#L1
	addq	$nhi,v0,$hi1	#U0

	umulh	$nj,$m1,$nhi	#U1
	s8addq	$j,$ap,$aj	#L0
	cmpult	$lo1,$lo0,v0	#L1
	cmplt	$j,$num,$tj	#U0	# borrow $tj

	addq	$hi0,AT,$hi0	#L0
	addq	$hi1,v0,$hi1	#U1
	stq	$lo1,-8($tp)	#L1
	bne	$tj,.Linner	#U0
	.set	reorder

	ldq	$tj,8($tp)
	addq	$alo,$hi0,$lo0
	addq	$nlo,$hi1,$lo1
	cmpult	$lo0,$hi0,AT
	cmpult	$lo1,$hi1,v0
	addq	$ahi,AT,$hi0
	addq	$nhi,v0,$hi1

	addq	$lo0,$tj,$lo0
	cmpult	$lo0,$tj,AT
	addq	$hi0,AT,$hi0

	ldq	$tj,16($tp)
	addq	$lo1,$lo0,$j
	cmpult	$j,$lo0,v0
	addq	$hi1,v0,$hi1

	addq	$hi1,$hi0,$lo1
	stq	$j,0($tp)
	cmpult	$lo1,$hi0,$hi1
	addq	$lo1,$tj,$lo1
	cmpult	$lo1,$tj,AT
	addl	$i,1,$i
	addq	$hi1,AT,$hi1
	stq	$lo1,8($tp)
	cmplt	$i,$num,$tj	# borrow $tj
	stq	$hi1,16($tp)
	bne	$tj,.Louter

	s8addq	$num,sp,$tj	# &tp[num]
	mov	$rp,$bp		# put rp aside
	mov	sp,$tp
	mov	sp,$ap
	mov	0,$hi0		# clear borrow bit

.align	4
.Lsub:	ldq	$lo0,0($tp)
	ldq	$lo1,0($np)
	lda	$tp,8($tp)
	lda	$np,8($np)
	subq	$lo0,$lo1,$lo1	# tp[i]-np[i]
	cmpult	$lo0,$lo1,AT
	subq	$lo1,$hi0,$lo0
	cmpult	$lo1,$lo0,$hi0
	or	$hi0,AT,$hi0
	stq	$lo0,0($rp)
	cmpult	$tp,$tj,v0
	lda	$rp,8($rp)
	bne	v0,.Lsub

	subq	$hi1,$hi0,$hi0	# handle upmost overflow bit
	mov	sp,$tp
	mov	$bp,$rp		# restore rp

	and	sp,$hi0,$ap
	bic	$bp,$hi0,$bp
	bis	$bp,$ap,$ap	# ap=borrow?tp:rp

.align	4
.Lcopy:	ldq	$aj,0($ap)	# copy or in-place refresh
	lda	$tp,8($tp)
	lda	$rp,8($rp)
	lda	$ap,8($ap)
	stq	zero,-8($tp)	# zap tp
	cmpult	$tp,$tj,AT
	stq	$aj,-8($rp)
	bne	AT,.Lcopy
	mov	1,v0

.Lexit:
	.set	noreorder
	mov	fp,sp
	/*ldq	ra,0(sp)*/
	ldq	s3,8(sp)
	ldq	s4,16(sp)
	ldq	s5,24(sp)
	ldq	fp,32(sp)
	lda	sp,48(sp)
	ret	(ra)
.end	bn_mul_mont
.align	2
___

print $code;
close STDOUT;
