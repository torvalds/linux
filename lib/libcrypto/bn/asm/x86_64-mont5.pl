#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# August 2011.
#
# Companion to x86_64-mont.pl that optimizes cache-timing attack
# countermeasures. The subroutines are produced by replacing bp[i]
# references in their x86_64-mont.pl counterparts with cache-neutral
# references to powers table computed in BN_mod_exp_mont_consttime.
# In addition subroutine that scatters elements of the powers table
# is implemented, so that scatter-/gathering can be tuned without
# bn_exp.c modifications.

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

# int bn_mul_mont_gather5(
$rp="%rdi";	# BN_ULONG *rp,
$ap="%rsi";	# const BN_ULONG *ap,
$bp="%rdx";	# const BN_ULONG *bp,
$np="%rcx";	# const BN_ULONG *np,
$n0="%r8";	# const BN_ULONG *n0,
$num="%r9";	# int num,
		# int idx);	# 0 to 2^5-1, "index" in $bp holding
				# pre-computed powers of a', interlaced
				# in such manner that b[0] is $bp[idx],
				# b[1] is [2^5+idx], etc.
$lo0="%r10";
$hi0="%r11";
$hi1="%r13";
$i="%r14";
$j="%r15";
$m0="%rbx";
$m1="%rbp";

$code=<<___;
.text

.globl	bn_mul_mont_gather5
.type	bn_mul_mont_gather5,\@function,6
.align	64
bn_mul_mont_gather5:
	_CET_ENDBR
	test	\$3,${num}d
	jnz	.Lmul_enter
	cmp	\$8,${num}d
	jb	.Lmul_enter
	jmp	.Lmul4x_enter

.align	16
.Lmul_enter:
	mov	${num}d,${num}d
	movd	`($win64?56:8)`(%rsp),%xmm5	# load 7th argument
	lea	.Linc(%rip),%r10
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

.Lmul_alloca:
	mov	%rsp,%rax
	lea	2($num),%r11
	neg	%r11
	lea	-264(%rsp,%r11,8),%rsp	# tp=alloca(8*(num+2)+256+8)
	and	\$-1024,%rsp		# minimize TLB usage

	mov	%rax,8(%rsp,$num,8)	# tp[num+1]=%rsp
.Lmul_body:
	lea	128($bp),%r12		# reassign $bp (+size optimization)
___
		$bp="%r12";
		$STRIDE=2**5*8;		# 5 is "window size"
		$N=$STRIDE/4;		# should match cache line size
$code.=<<___;
	movdqa	0(%r10),%xmm0		# 00000001000000010000000000000000
	movdqa	16(%r10),%xmm1		# 00000002000000020000000200000002
	lea	24-112(%rsp,$num,8),%r10# place the mask after tp[num+3] (+ICache optimization)
	and	\$-16,%r10

	pshufd	\$0,%xmm5,%xmm5		# broadcast index
	movdqa	%xmm1,%xmm4
	movdqa	%xmm1,%xmm2
___
########################################################################
# calculate mask by comparing 0..31 to index and save result to stack
#
$code.=<<___;
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0		# compare to 1,0
	.byte	0x67
	movdqa	%xmm4,%xmm3
___
for($k=0;$k<$STRIDE/16-4;$k+=4) {
$code.=<<___;
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1		# compare to 3,2
	movdqa	%xmm0,`16*($k+0)+112`(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2		# compare to 5,4
	movdqa	%xmm1,`16*($k+1)+112`(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3		# compare to 7,6
	movdqa	%xmm2,`16*($k+2)+112`(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,`16*($k+3)+112`(%r10)
	movdqa	%xmm4,%xmm3
___
}
$code.=<<___;				# last iteration can be optimized
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,`16*($k+0)+112`(%r10)

	paddd	%xmm2,%xmm3
	.byte	0x67
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,`16*($k+1)+112`(%r10)

	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,`16*($k+2)+112`(%r10)
	pand	`16*($k+0)-128`($bp),%xmm0	# while it's still in register

	pand	`16*($k+1)-128`($bp),%xmm1
	pand	`16*($k+2)-128`($bp),%xmm2
	movdqa	%xmm3,`16*($k+3)+112`(%r10)
	pand	`16*($k+3)-128`($bp),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
___
for($k=0;$k<$STRIDE/16-4;$k+=4) {
$code.=<<___;
	movdqa	`16*($k+0)-128`($bp),%xmm4
	movdqa	`16*($k+1)-128`($bp),%xmm5
	movdqa	`16*($k+2)-128`($bp),%xmm2
	pand	`16*($k+0)+112`(%r10),%xmm4
	movdqa	`16*($k+3)-128`($bp),%xmm3
	pand	`16*($k+1)+112`(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	`16*($k+2)+112`(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	`16*($k+3)+112`(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
___
}
$code.=<<___;
	por	%xmm1,%xmm0
	pshufd	\$0x4e,%xmm0,%xmm1
	por	%xmm1,%xmm0
	lea	$STRIDE($bp),$bp
	movd	%xmm0,$m0		# m0=bp[0]

	mov	($n0),$n0		# pull n0[0] value
	mov	($ap),%rax

	xor	$i,$i			# i=0
	xor	$j,$j			# j=0

	mov	$n0,$m1
	mulq	$m0			# ap[0]*bp[0]
	mov	%rax,$lo0
	mov	($np),%rax

	imulq	$lo0,$m1		# "tp[0]"*n0
	mov	%rdx,$hi0

	mulq	$m1			# np[0]*m1
	add	%rax,$lo0		# discarded
	mov	8($ap),%rax
	adc	\$0,%rdx
	mov	%rdx,$hi1

	lea	1($j),$j		# j++
	jmp	.L1st_enter

.align	16
.L1st:
	add	%rax,$hi1
	mov	($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$hi0,$hi1		# np[j]*m1+ap[j]*bp[0]
	mov	$lo0,$hi0
	adc	\$0,%rdx
	mov	$hi1,-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$hi1

.L1st_enter:
	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$hi0
	mov	($np,$j,8),%rax
	adc	\$0,%rdx
	lea	1($j),$j		# j++
	mov	%rdx,$lo0

	mulq	$m1			# np[j]*m1
	cmp	$num,$j
	jl	.L1st

	add	%rax,$hi1
	mov	($ap),%rax		# ap[0]
	adc	\$0,%rdx
	add	$hi0,$hi1		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$hi1,-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$hi1
	mov	$lo0,$hi0

	xor	%rdx,%rdx
	add	$hi0,$hi1
	adc	\$0,%rdx
	mov	$hi1,-8(%rsp,$num,8)
	mov	%rdx,(%rsp,$num,8)	# store upmost overflow bit

	lea	1($i),$i		# i++
	jmp	.Louter
.align	16
.Louter:
	lea	24+128(%rsp,$num,8),%rdx	# where 256-byte mask is (+size optimization)
	and	\$-16,%rdx
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
for($k=0;$k<$STRIDE/16;$k+=4) {
$code.=<<___;
	movdqa	`16*($k+0)-128`($bp),%xmm0
	movdqa	`16*($k+1)-128`($bp),%xmm1
	movdqa	`16*($k+2)-128`($bp),%xmm2
	movdqa	`16*($k+3)-128`($bp),%xmm3
	pand	`16*($k+0)-128`(%rdx),%xmm0
	pand	`16*($k+1)-128`(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	`16*($k+2)-128`(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	`16*($k+3)-128`(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
___
}
$code.=<<___;
	por	%xmm5,%xmm4
	pshufd	\$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	lea	$STRIDE($bp),$bp
	movd	%xmm0,$m0		# m0=bp[i]

	xor	$j,$j			# j=0
	mov	$n0,$m1
	mov	(%rsp),$lo0

	mulq	$m0			# ap[0]*bp[i]
	add	%rax,$lo0		# ap[0]*bp[i]+tp[0]
	mov	($np),%rax
	adc	\$0,%rdx

	imulq	$lo0,$m1		# tp[0]*n0
	mov	%rdx,$hi0

	mulq	$m1			# np[0]*m1
	add	%rax,$lo0		# discarded
	mov	8($ap),%rax
	adc	\$0,%rdx
	mov	8(%rsp),$lo0		# tp[1]
	mov	%rdx,$hi1

	lea	1($j),$j		# j++
	jmp	.Linner_enter

.align	16
.Linner:
	add	%rax,$hi1
	mov	($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$lo0,$hi1		# np[j]*m1+ap[j]*bp[i]+tp[j]
	mov	(%rsp,$j,8),$lo0
	adc	\$0,%rdx
	mov	$hi1,-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$hi1

.Linner_enter:
	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$hi0
	mov	($np,$j,8),%rax
	adc	\$0,%rdx
	add	$hi0,$lo0		# ap[j]*bp[i]+tp[j]
	mov	%rdx,$hi0
	adc	\$0,$hi0
	lea	1($j),$j		# j++

	mulq	$m1			# np[j]*m1
	cmp	$num,$j
	jl	.Linner

	add	%rax,$hi1
	mov	($ap),%rax		# ap[0]
	adc	\$0,%rdx
	add	$lo0,$hi1		# np[j]*m1+ap[j]*bp[i]+tp[j]
	mov	(%rsp,$j,8),$lo0
	adc	\$0,%rdx
	mov	$hi1,-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$hi1

	xor	%rdx,%rdx
	add	$hi0,$hi1
	adc	\$0,%rdx
	add	$lo0,$hi1		# pull upmost overflow bit
	adc	\$0,%rdx
	mov	$hi1,-8(%rsp,$num,8)
	mov	%rdx,(%rsp,$num,8)	# store upmost overflow bit

	lea	1($i),$i		# i++
	cmp	$num,$i
	jl	.Louter

	xor	$i,$i			# i=0 and clear CF!
	mov	(%rsp),%rax		# tp[0]
	lea	(%rsp),$ap		# borrow ap for tp
	mov	$num,$j			# j=num
	jmp	.Lsub
.align	16
.Lsub:	sbb	($np,$i,8),%rax
	mov	%rax,($rp,$i,8)		# rp[i]=tp[i]-np[i]
	mov	8($ap,$i,8),%rax	# tp[i+1]
	lea	1($i),$i		# i++
	dec	$j			# doesnn't affect CF!
	jnz	.Lsub

	sbb	\$0,%rax		# handle upmost overflow bit
	xor	$i,$i
	and	%rax,$ap
	not	%rax
	mov	$rp,$np
	and	%rax,$np
	mov	$num,$j			# j=num
	or	$np,$ap			# ap=borrow?tp:rp
.align	16
.Lcopy:					# copy or in-place refresh
	mov	($ap,$i,8),%rax
	mov	$i,(%rsp,$i,8)		# zap temporary vector
	mov	%rax,($rp,$i,8)		# rp[i]=tp[i]
	lea	1($i),$i
	sub	\$1,$j
	jnz	.Lcopy

	mov	8(%rsp,$num,8),%rsi	# restore %rsp
	mov	\$1,%rax

	mov	(%rsi),%r15
	mov	8(%rsi),%r14
	mov	16(%rsi),%r13
	mov	24(%rsi),%r12
	mov	32(%rsi),%rbp
	mov	40(%rsi),%rbx
	lea	48(%rsi),%rsp
.Lmul_epilogue:
	ret
.size	bn_mul_mont_gather5,.-bn_mul_mont_gather5
___
{{{
my @A=("%r10","%r11");
my @N=("%r13","%rdi");
$code.=<<___;
.type	bn_mul4x_mont_gather5,\@function,6
.align	16
bn_mul4x_mont_gather5:
	_CET_ENDBR
.Lmul4x_enter:
	mov	${num}d,${num}d
	movd	`($win64?56:8)`(%rsp),%xmm5	# load 7th argument
	lea	.Linc(%rip),%r10
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

.Lmul4x_alloca:
	mov	%rsp,%rax
	lea	4($num),%r11
	neg	%r11
	lea	-256(%rsp,%r11,8),%rsp	# tp=alloca(8*(num+4)+256)
	and	\$-1024,%rsp		# minimize TLB usage

	mov	%rax,8(%rsp,$num,8)	# tp[num+1]=%rsp
.Lmul4x_body:
	mov	$rp,16(%rsp,$num,8)	# tp[num+2]=$rp
	lea	128(%rdx),%r12		# reassign $bp (+size optimization)
___
		$bp="%r12";
		$STRIDE=2**5*8;		# 5 is "window size"
		$N=$STRIDE/4;		# should match cache line size
$code.=<<___;
	movdqa	0(%r10),%xmm0		# 00000001000000010000000000000000
	movdqa	16(%r10),%xmm1		# 00000002000000020000000200000002
	lea	32-112(%rsp,$num,8),%r10# place the mask after tp[num+4] (+ICache optimization)

	pshufd	\$0,%xmm5,%xmm5		# broadcast index
	movdqa	%xmm1,%xmm4
	.byte	0x67,0x67
	movdqa	%xmm1,%xmm2
___
########################################################################
# calculate mask by comparing 0..31 to index and save result to stack
#
$code.=<<___;
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0		# compare to 1,0
	.byte	0x67
	movdqa	%xmm4,%xmm3
___
for($k=0;$k<$STRIDE/16-4;$k+=4) {
$code.=<<___;
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1		# compare to 3,2
	movdqa	%xmm0,`16*($k+0)+112`(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2		# compare to 5,4
	movdqa	%xmm1,`16*($k+1)+112`(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3		# compare to 7,6
	movdqa	%xmm2,`16*($k+2)+112`(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,`16*($k+3)+112`(%r10)
	movdqa	%xmm4,%xmm3
___
}
$code.=<<___;				# last iteration can be optimized
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,`16*($k+0)+112`(%r10)

	paddd	%xmm2,%xmm3
	.byte	0x67
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,`16*($k+1)+112`(%r10)

	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,`16*($k+2)+112`(%r10)
	pand	`16*($k+0)-128`($bp),%xmm0	# while it's still in register

	pand	`16*($k+1)-128`($bp),%xmm1
	pand	`16*($k+2)-128`($bp),%xmm2
	movdqa	%xmm3,`16*($k+3)+112`(%r10)
	pand	`16*($k+3)-128`($bp),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
___
for($k=0;$k<$STRIDE/16-4;$k+=4) {
$code.=<<___;
	movdqa	`16*($k+0)-128`($bp),%xmm4
	movdqa	`16*($k+1)-128`($bp),%xmm5
	movdqa	`16*($k+2)-128`($bp),%xmm2
	pand	`16*($k+0)+112`(%r10),%xmm4
	movdqa	`16*($k+3)-128`($bp),%xmm3
	pand	`16*($k+1)+112`(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	`16*($k+2)+112`(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	`16*($k+3)+112`(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
___
}
$code.=<<___;
	por	%xmm1,%xmm0
	pshufd	\$0x4e,%xmm0,%xmm1
	por	%xmm1,%xmm0
	lea	$STRIDE($bp),$bp
	movd	%xmm0,$m0		# m0=bp[0]

	mov	($n0),$n0		# pull n0[0] value
	mov	($ap),%rax

	xor	$i,$i			# i=0
	xor	$j,$j			# j=0

	mov	$n0,$m1
	mulq	$m0			# ap[0]*bp[0]
	mov	%rax,$A[0]
	mov	($np),%rax

	imulq	$A[0],$m1		# "tp[0]"*n0
	mov	%rdx,$A[1]

	mulq	$m1			# np[0]*m1
	add	%rax,$A[0]		# discarded
	mov	8($ap),%rax
	adc	\$0,%rdx
	mov	%rdx,$N[1]

	mulq	$m0
	add	%rax,$A[1]
	mov	8($np),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[0]

	mulq	$m1
	add	%rax,$N[1]
	mov	16($ap),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]
	lea	4($j),$j		# j++
	adc	\$0,%rdx
	mov	$N[1],(%rsp)
	mov	%rdx,$N[0]
	jmp	.L1st4x
.align	16
.L1st4x:
	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[0]
	mov	-16($np,$j,8),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	-8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[1]
	mov	-8($np,$j,8),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]

	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[0]
	mov	($np,$j,8),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[0],-8(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[1]
	mov	8($np,$j,8),%rax
	adc	\$0,%rdx
	lea	4($j),$j		# j++
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	-16($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[1],-32(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]
	cmp	$num,$j
	jl	.L1st4x

	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[0]
	mov	-16($np,$j,8),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	-8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[0]
	add	%rax,$A[1]
	mov	-8($np,$j,8),%rax
	adc	\$0,%rdx
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	($ap),%rax		# ap[0]
	adc	\$0,%rdx
	add	$A[1],$N[1]		# np[j]*m1+ap[j]*bp[0]
	adc	\$0,%rdx
	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]

	xor	$N[1],$N[1]
	add	$A[0],$N[0]
	adc	\$0,$N[1]
	mov	$N[0],-8(%rsp,$j,8)
	mov	$N[1],(%rsp,$j,8)	# store upmost overflow bit

	lea	1($i),$i		# i++
.align	4
.Louter4x:
	lea	32+128(%rsp,$num,8),%rdx	# where 256-byte mask is (+size optimization)
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
for($k=0;$k<$STRIDE/16;$k+=4) {
$code.=<<___;
	movdqa	`16*($k+0)-128`($bp),%xmm0
	movdqa	`16*($k+1)-128`($bp),%xmm1
	movdqa	`16*($k+2)-128`($bp),%xmm2
	movdqa	`16*($k+3)-128`($bp),%xmm3
	pand	`16*($k+0)-128`(%rdx),%xmm0
	pand	`16*($k+1)-128`(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	`16*($k+2)-128`(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	`16*($k+3)-128`(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
___
}
$code.=<<___;
	por	%xmm5,%xmm4
	pshufd	\$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	lea	$STRIDE($bp),$bp
	movd	%xmm0,$m0		# m0=bp[i]

	xor	$j,$j			# j=0

	mov	(%rsp),$A[0]
	mov	$n0,$m1
	mulq	$m0			# ap[0]*bp[i]
	add	%rax,$A[0]		# ap[0]*bp[i]+tp[0]
	mov	($np),%rax
	adc	\$0,%rdx

	imulq	$A[0],$m1		# tp[0]*n0
	mov	%rdx,$A[1]

	mulq	$m1			# np[0]*m1
	add	%rax,$A[0]		# "$N[0]", discarded
	mov	8($ap),%rax
	adc	\$0,%rdx
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[1]
	mov	8($np),%rax
	adc	\$0,%rdx
	add	8(%rsp),$A[1]		# +tp[1]
	adc	\$0,%rdx
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	16($ap),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]		# np[j]*m1+ap[j]*bp[i]+tp[j]
	lea	4($j),$j		# j+=2
	adc	\$0,%rdx
	mov	%rdx,$N[0]
	jmp	.Linner4x
.align	16
.Linner4x:
	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[0]
	mov	-16($np,$j,8),%rax
	adc	\$0,%rdx
	add	-16(%rsp,$j,8),$A[0]	# ap[j]*bp[i]+tp[j]
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	-8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]
	adc	\$0,%rdx
	mov	$N[1],-32(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[1]
	mov	-8($np,$j,8),%rax
	adc	\$0,%rdx
	add	-8(%rsp,$j,8),$A[1]
	adc	\$0,%rdx
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]
	adc	\$0,%rdx
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[0]
	mov	($np,$j,8),%rax
	adc	\$0,%rdx
	add	(%rsp,$j,8),$A[0]	# ap[j]*bp[i]+tp[j]
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]
	adc	\$0,%rdx
	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[1]
	mov	8($np,$j,8),%rax
	adc	\$0,%rdx
	add	8(%rsp,$j,8),$A[1]
	adc	\$0,%rdx
	lea	4($j),$j		# j++
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	-16($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[1],$N[1]
	adc	\$0,%rdx
	mov	$N[0],-40(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]
	cmp	$num,$j
	jl	.Linner4x

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[0]
	mov	-16($np,$j,8),%rax
	adc	\$0,%rdx
	add	-16(%rsp,$j,8),$A[0]	# ap[j]*bp[i]+tp[j]
	adc	\$0,%rdx
	mov	%rdx,$A[1]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[0]
	mov	-8($ap,$j,8),%rax
	adc	\$0,%rdx
	add	$A[0],$N[0]
	adc	\$0,%rdx
	mov	$N[1],-32(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[1]

	mulq	$m0			# ap[j]*bp[i]
	add	%rax,$A[1]
	mov	-8($np,$j,8),%rax
	adc	\$0,%rdx
	add	-8(%rsp,$j,8),$A[1]
	adc	\$0,%rdx
	lea	1($i),$i		# i++
	mov	%rdx,$A[0]

	mulq	$m1			# np[j]*m1
	add	%rax,$N[1]
	mov	($ap),%rax		# ap[0]
	adc	\$0,%rdx
	add	$A[1],$N[1]
	adc	\$0,%rdx
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]

	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]

	xor	$N[1],$N[1]
	add	$A[0],$N[0]
	adc	\$0,$N[1]
	add	(%rsp,$num,8),$N[0]	# pull upmost overflow bit
	adc	\$0,$N[1]
	mov	$N[0],-8(%rsp,$j,8)
	mov	$N[1],(%rsp,$j,8)	# store upmost overflow bit

	cmp	$num,$i
	jl	.Louter4x
___
{
my @ri=("%rax","%rdx",$m0,$m1);
$code.=<<___;
	mov	16(%rsp,$num,8),$rp	# restore $rp
	mov	0(%rsp),@ri[0]		# tp[0]
	pxor	%xmm0,%xmm0
	mov	8(%rsp),@ri[1]		# tp[1]
	shr	\$2,$num		# num/=4
	lea	(%rsp),$ap		# borrow ap for tp
	xor	$i,$i			# i=0 and clear CF!

	sub	0($np),@ri[0]
	mov	16($ap),@ri[2]		# tp[2]
	mov	24($ap),@ri[3]		# tp[3]
	sbb	8($np),@ri[1]
	lea	-1($num),$j		# j=num/4-1
	jmp	.Lsub4x
.align	16
.Lsub4x:
	mov	@ri[0],0($rp,$i,8)	# rp[i]=tp[i]-np[i]
	mov	@ri[1],8($rp,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	16($np,$i,8),@ri[2]
	mov	32($ap,$i,8),@ri[0]	# tp[i+1]
	mov	40($ap,$i,8),@ri[1]
	sbb	24($np,$i,8),@ri[3]
	mov	@ri[2],16($rp,$i,8)	# rp[i]=tp[i]-np[i]
	mov	@ri[3],24($rp,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	32($np,$i,8),@ri[0]
	mov	48($ap,$i,8),@ri[2]
	mov	56($ap,$i,8),@ri[3]
	sbb	40($np,$i,8),@ri[1]
	lea	4($i),$i		# i++
	dec	$j			# doesnn't affect CF!
	jnz	.Lsub4x

	mov	@ri[0],0($rp,$i,8)	# rp[i]=tp[i]-np[i]
	mov	32($ap,$i,8),@ri[0]	# load overflow bit
	sbb	16($np,$i,8),@ri[2]
	mov	@ri[1],8($rp,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	24($np,$i,8),@ri[3]
	mov	@ri[2],16($rp,$i,8)	# rp[i]=tp[i]-np[i]

	sbb	\$0,@ri[0]		# handle upmost overflow bit
	mov	@ri[3],24($rp,$i,8)	# rp[i]=tp[i]-np[i]
	xor	$i,$i			# i=0
	and	@ri[0],$ap
	not	@ri[0]
	mov	$rp,$np
	and	@ri[0],$np
	lea	-1($num),$j
	or	$np,$ap			# ap=borrow?tp:rp

	movdqu	($ap),%xmm1
	movdqa	%xmm0,(%rsp)
	movdqu	%xmm1,($rp)
	jmp	.Lcopy4x
.align	16
.Lcopy4x:					# copy or in-place refresh
	movdqu	16($ap,$i),%xmm2
	movdqu	32($ap,$i),%xmm1
	movdqa	%xmm0,16(%rsp,$i)
	movdqu	%xmm2,16($rp,$i)
	movdqa	%xmm0,32(%rsp,$i)
	movdqu	%xmm1,32($rp,$i)
	lea	32($i),$i
	dec	$j
	jnz	.Lcopy4x

	shl	\$2,$num
	movdqu	16($ap,$i),%xmm2
	movdqa	%xmm0,16(%rsp,$i)
	movdqu	%xmm2,16($rp,$i)
___
}
$code.=<<___;
	mov	8(%rsp,$num,8),%rsi	# restore %rsp
	mov	\$1,%rax

	mov	(%rsi),%r15
	mov	8(%rsi),%r14
	mov	16(%rsi),%r13
	mov	24(%rsi),%r12
	mov	32(%rsi),%rbp
	mov	40(%rsi),%rbx
	lea	48(%rsi),%rsp
.Lmul4x_epilogue:
	ret
.size	bn_mul4x_mont_gather5,.-bn_mul4x_mont_gather5
___
}}}

{
my ($inp,$num,$tbl,$idx)=$win64?("%rcx","%rdx","%r8", "%r9d") : # Win64 order
				("%rdi","%rsi","%rdx","%ecx"); # Unix order
my $out=$inp;
my $STRIDE=2**5*8;
my $N=$STRIDE/4;

$code.=<<___;
.globl	bn_scatter5
.type	bn_scatter5,\@abi-omnipotent
.align	16
bn_scatter5:
	_CET_ENDBR
	cmp	\$0, $num
	jz	.Lscatter_epilogue
	lea	($tbl,$idx,8),$tbl
.Lscatter:
	mov	($inp),%rax
	lea	8($inp),$inp
	mov	%rax,($tbl)
	lea	32*8($tbl),$tbl
	sub	\$1,$num
	jnz	.Lscatter
.Lscatter_epilogue:
	ret
.size	bn_scatter5,.-bn_scatter5

.globl	bn_gather5
.type	bn_gather5,\@abi-omnipotent
.align	16
bn_gather5:
	_CET_ENDBR
.LSEH_begin_bn_gather5:			# Win64 thing, but harmless in other cases
	# I can't trust assembler to use specific encoding:-(
	.byte	0x4c,0x8d,0x14,0x24			# lea    (%rsp),%r10
	.byte	0x48,0x81,0xec,0x08,0x01,0x00,0x00	# sub	$0x108,%rsp
	lea	.Linc(%rip),%rax
	and	\$-16,%rsp		# shouldn't be formally required

	movd	$idx,%xmm5
	movdqa	0(%rax),%xmm0		# 00000001000000010000000000000000
	movdqa	16(%rax),%xmm1		# 00000002000000020000000200000002
	lea	128($tbl),%r11		# size optimization
	lea	128(%rsp),%rax		# size optimization

	pshufd	\$0,%xmm5,%xmm5		# broadcast $idx
	movdqa	%xmm1,%xmm4
	movdqa	%xmm1,%xmm2
___
########################################################################
# calculate mask by comparing 0..31 to $idx and save result to stack
#
for($i=0;$i<$STRIDE/16;$i+=4) {
$code.=<<___;
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0		# compare to 1,0
___
$code.=<<___	if ($i);
	movdqa	%xmm3,`16*($i-1)-128`(%rax)
___
$code.=<<___;
	movdqa	%xmm4,%xmm3

	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1		# compare to 3,2
	movdqa	%xmm0,`16*($i+0)-128`(%rax)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2		# compare to 5,4
	movdqa	%xmm1,`16*($i+1)-128`(%rax)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3		# compare to 7,6
	movdqa	%xmm2,`16*($i+2)-128`(%rax)
	movdqa	%xmm4,%xmm2
___
}
$code.=<<___;
	movdqa	%xmm3,`16*($i-1)-128`(%rax)
	jmp	.Lgather

.align	32
.Lgather:
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
for($i=0;$i<$STRIDE/16;$i+=4) {
$code.=<<___;
	movdqa	`16*($i+0)-128`(%r11),%xmm0
	movdqa	`16*($i+1)-128`(%r11),%xmm1
	movdqa	`16*($i+2)-128`(%r11),%xmm2
	pand	`16*($i+0)-128`(%rax),%xmm0
	movdqa	`16*($i+3)-128`(%r11),%xmm3
	pand	`16*($i+1)-128`(%rax),%xmm1
	por	%xmm0,%xmm4
	pand	`16*($i+2)-128`(%rax),%xmm2
	por	%xmm1,%xmm5
	pand	`16*($i+3)-128`(%rax),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
___
}
$code.=<<___;
	por	%xmm5,%xmm4
	lea	$STRIDE(%r11),%r11
	pshufd	\$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	movq	%xmm0,($out)		# m0=bp[0]
	lea	8($out),$out
	sub	\$1,$num
	jnz	.Lgather

	lea	(%r10),%rsp
	ret
.LSEH_end_bn_gather5:
.size	bn_gather5,.-bn_gather5
___
}
$code.=<<___;
.section .rodata
.align	64
.Linc:
	.long	0,0, 1,1
	.long	2,2, 2,2
.text
___

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	mul_handler,\@abi-omnipotent
.align	16
mul_handler:
	_CET_ENDBR
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# end of prologue label
	cmp	%r10,%rbx		# context->Rip<end of prologue label
	jb	.Lcommon_seh_tail

	lea	48(%rax),%rax

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# end of alloca label
	cmp	%r10,%rbx		# context->Rip<end of alloca label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	8(%r11),%r10d		# HandlerData[2]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	192($context),%r10	# pull $num
	mov	8(%rax,%r10,8),%rax	# pull saved stack pointer

	lea	48(%rax),%rax

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	-48(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

.Lcommon_seh_tail:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$154,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	mul_handler,.-mul_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_bn_mul_mont_gather5
	.rva	.LSEH_end_bn_mul_mont_gather5
	.rva	.LSEH_info_bn_mul_mont_gather5

	.rva	.LSEH_begin_bn_mul4x_mont_gather5
	.rva	.LSEH_end_bn_mul4x_mont_gather5
	.rva	.LSEH_info_bn_mul4x_mont_gather5

	.rva	.LSEH_begin_bn_gather5
	.rva	.LSEH_end_bn_gather5
	.rva	.LSEH_info_bn_gather5

.section	.xdata
.align	8
.LSEH_info_bn_mul_mont_gather5:
	.byte	9,0,0,0
	.rva	mul_handler
	.rva	.Lmul_alloca,.Lmul_body,.Lmul_epilogue		# HandlerData[]
.align	8
.LSEH_info_bn_mul4x_mont_gather5:
	.byte	9,0,0,0
	.rva	mul_handler
	.rva	.Lmul4x_alloca,.Lmul4x_body,.Lmul4x_epilogue	# HandlerData[]
.align	8
.LSEH_info_bn_gather5:
	.byte	0x01,0x0b,0x03,0x0a
	.byte	0x0b,0x01,0x21,0x00	# sub	rsp,0x108
	.byte	0x04,0xa3,0x00,0x00	# lea	r10,(rsp), set_frame r10
.align	8
___
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;
close STDOUT;
