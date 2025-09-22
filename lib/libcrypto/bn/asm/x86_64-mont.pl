#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# October 2005.
#
# Montgomery multiplication routine for x86_64. While it gives modest
# 9% improvement of rsa4096 sign on Opteron, rsa512 sign runs more
# than twice, >2x, as fast. Most common rsa1024 sign is improved by
# respectful 50%. It remains to be seen if loop unrolling and
# dedicated squaring routine can provide further improvement...

# July 2011.
#
# Add dedicated squaring procedure. Performance improvement varies
# from platform to platform, but in average it's ~5%/15%/25%/33%
# for 512-/1024-/2048-/4096-bit RSA *sign* benchmarks respectively.

# August 2011.
#
# Unroll and modulo-schedule inner loops in such manner that they
# are "fallen through" for input lengths of 8, which is critical for
# 1024-bit RSA *sign*. Average performance improvement in comparison
# to *initial* version of this module from 2005 is ~0%/30%/40%/45%
# for 512-/1024-/2048-/4096-bit RSA *sign* benchmarks respectively.

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

# int bn_mul_mont(
$rp="%rdi";	# BN_ULONG *rp,
$ap="%rsi";	# const BN_ULONG *ap,
$bp="%rdx";	# const BN_ULONG *bp,
$np="%rcx";	# const BN_ULONG *np,
$n0="%r8";	# const BN_ULONG *n0,
$num="%r9";	# int num);
$lo0="%r10";
$hi0="%r11";
$hi1="%r13";
$i="%r14";
$j="%r15";
$m0="%rbx";
$m1="%rbp";

$code=<<___;
.text

.globl	bn_mul_mont
.type	bn_mul_mont,\@function,6
.align	16
bn_mul_mont:
	_CET_ENDBR
	test	\$3,${num}d
	jnz	.Lmul_enter
	cmp	\$8,${num}d
	jb	.Lmul_enter
	cmp	$ap,$bp
	jne	.Lmul4x_enter
	jmp	.Lsqr4x_enter

.align	16
.Lmul_enter:
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	mov	${num}d,${num}d
	lea	2($num),%r10
	mov	%rsp,%r11
	neg	%r10
	lea	(%rsp,%r10,8),%rsp	# tp=alloca(8*(num+2))
	and	\$-1024,%rsp		# minimize TLB usage

	mov	%r11,8(%rsp,$num,8)	# tp[num+1]=%rsp
.Lmul_body:
	mov	$bp,%r12		# reassign $bp
___
		$bp="%r12";
$code.=<<___;
	mov	($n0),$n0		# pull n0[0] value
	mov	($bp),$m0		# m0=bp[0]
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
	mov	($bp,$i,8),$m0		# m0=bp[i]
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
.size	bn_mul_mont,.-bn_mul_mont
___
{{{
my @A=("%r10","%r11");
my @N=("%r13","%rdi");
$code.=<<___;
.type	bn_mul4x_mont,\@function,6
.align	16
bn_mul4x_mont:
.Lmul4x_enter:
	_CET_ENDBR
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	mov	${num}d,${num}d
	lea	4($num),%r10
	mov	%rsp,%r11
	neg	%r10
	lea	(%rsp,%r10,8),%rsp	# tp=alloca(8*(num+4))
	and	\$-1024,%rsp		# minimize TLB usage

	mov	%r11,8(%rsp,$num,8)	# tp[num+1]=%rsp
.Lmul4x_body:
	mov	$rp,16(%rsp,$num,8)	# tp[num+2]=$rp
	mov	%rdx,%r12		# reassign $bp
___
		$bp="%r12";
$code.=<<___;
	mov	($n0),$n0		# pull n0[0] value
	mov	($bp),$m0		# m0=bp[0]
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
	mov	($bp,$i,8),$m0		# m0=bp[i]
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
	mov	$N[1],(%rsp)		# tp[j-1]
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
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
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
	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]
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
	mov	$N[0],-8(%rsp,$j,8)	# tp[j-1]
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
	mov	$N[1],-32(%rsp,$j,8)	# tp[j-1]
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
	mov	$N[0],-24(%rsp,$j,8)	# tp[j-1]
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
	mov	$N[1],-16(%rsp,$j,8)	# tp[j-1]
	mov	%rdx,$N[0]

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
.size	bn_mul4x_mont,.-bn_mul4x_mont
___
}}}
{{{
######################################################################
# void bn_sqr4x_mont(
my $rptr="%rdi";	# const BN_ULONG *rptr,
my $aptr="%rsi";	# const BN_ULONG *aptr,
my $bptr="%rdx";	# not used
my $nptr="%rcx";	# const BN_ULONG *nptr,
my $n0  ="%r8";		# const BN_ULONG *n0);
my $num ="%r9";		# int num, has to be divisible by 4 and
			# not less than 8

my ($i,$j,$tptr)=("%rbp","%rcx",$rptr);
my @A0=("%r10","%r11");
my @A1=("%r12","%r13");
my ($a0,$a1,$ai)=("%r14","%r15","%rbx");

$code.=<<___;
.type	bn_sqr4x_mont,\@function,6
.align	16
bn_sqr4x_mont:
.Lsqr4x_enter:
	_CET_ENDBR
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	shl	\$3,${num}d		# convert $num to bytes
	xor	%r10,%r10
	mov	%rsp,%r11		# put aside %rsp
	sub	$num,%r10		# -$num
	mov	($n0),$n0		# *n0
	lea	-72(%rsp,%r10,2),%rsp	# alloca(frame+2*$num)
	and	\$-1024,%rsp		# minimize TLB usage
	##############################################################
	# Stack layout
	#
	# +0	saved $num, used in reduction section
	# +8	&t[2*$num], used in reduction section
	# +32	saved $rptr
	# +40	saved $nptr
	# +48	saved *n0
	# +56	saved %rsp
	# +64	t[2*$num]
	#
	mov	$rptr,32(%rsp)		# save $rptr
	mov	$nptr,40(%rsp)
	mov	$n0,  48(%rsp)
	mov	%r11, 56(%rsp)		# save original %rsp
.Lsqr4x_body:
	##############################################################
	# Squaring part:
	#
	# a) multiply-n-add everything but a[i]*a[i];
	# b) shift result of a) by 1 to the left and accumulate
	#    a[i]*a[i] products;
	#
	lea	32(%r10),$i		# $i=-($num-32)
	lea	($aptr,$num),$aptr	# end of a[] buffer, ($aptr,$i)=&ap[2]

	mov	$num,$j			# $j=$num

					# comments apply to $num==8 case
	mov	-32($aptr,$i),$a0	# a[0]
	lea	64(%rsp,$num,2),$tptr	# end of tp[] buffer, &tp[2*$num]
	mov	-24($aptr,$i),%rax	# a[1]
	lea	-32($tptr,$i),$tptr	# end of tp[] window, &tp[2*$num-"$i"]
	mov	-16($aptr,$i),$ai	# a[2]
	mov	%rax,$a1

	mul	$a0			# a[1]*a[0]
	mov	%rax,$A0[0]		# a[1]*a[0]
	 mov	$ai,%rax		# a[2]
	mov	%rdx,$A0[1]
	mov	$A0[0],-24($tptr,$i)	# t[1]

	xor	$A0[0],$A0[0]
	mul	$a0			# a[2]*a[0]
	add	%rax,$A0[1]
	 mov	$ai,%rax
	adc	%rdx,$A0[0]
	mov	$A0[1],-16($tptr,$i)	# t[2]

	lea	-16($i),$j		# j=-16


	 mov	8($aptr,$j),$ai		# a[3]
	mul	$a1			# a[2]*a[1]
	mov	%rax,$A1[0]		# a[2]*a[1]+t[3]
	 mov	$ai,%rax
	mov	%rdx,$A1[1]

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	 lea	16($j),$j
	adc	\$0,$A0[1]
	mul	$a0			# a[3]*a[0]
	add	%rax,$A0[0]		# a[3]*a[0]+a[2]*a[1]+t[3]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],-8($tptr,$j)	# t[3]
	jmp	.Lsqr4x_1st

.align	16
.Lsqr4x_1st:
	 mov	($aptr,$j),$ai		# a[4]
	xor	$A1[0],$A1[0]
	mul	$a1			# a[3]*a[1]
	add	%rax,$A1[1]		# a[3]*a[1]+t[4]
	 mov	$ai,%rax
	adc	%rdx,$A1[0]

	xor	$A0[0],$A0[0]
	add	$A1[1],$A0[1]
	adc	\$0,$A0[0]
	mul	$a0			# a[4]*a[0]
	add	%rax,$A0[1]		# a[4]*a[0]+a[3]*a[1]+t[4]
	 mov	$ai,%rax		# a[3]
	adc	%rdx,$A0[0]
	mov	$A0[1],($tptr,$j)	# t[4]


	 mov	8($aptr,$j),$ai		# a[5]
	xor	$A1[1],$A1[1]
	mul	$a1			# a[4]*a[3]
	add	%rax,$A1[0]		# a[4]*a[3]+t[5]
	 mov	$ai,%rax
	adc	%rdx,$A1[1]

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	adc	\$0,$A0[1]
	mul	$a0			# a[5]*a[2]
	add	%rax,$A0[0]		# a[5]*a[2]+a[4]*a[3]+t[5]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],8($tptr,$j)	# t[5]

	 mov	16($aptr,$j),$ai	# a[6]
	xor	$A1[0],$A1[0]
	mul	$a1			# a[5]*a[3]
	add	%rax,$A1[1]		# a[5]*a[3]+t[6]
	 mov	$ai,%rax
	adc	%rdx,$A1[0]

	xor	$A0[0],$A0[0]
	add	$A1[1],$A0[1]
	adc	\$0,$A0[0]
	mul	$a0			# a[6]*a[2]
	add	%rax,$A0[1]		# a[6]*a[2]+a[5]*a[3]+t[6]
	 mov	$ai,%rax		# a[3]
	adc	%rdx,$A0[0]
	mov	$A0[1],16($tptr,$j)	# t[6]


	 mov	24($aptr,$j),$ai	# a[7]
	xor	$A1[1],$A1[1]
	mul	$a1			# a[6]*a[5]
	add	%rax,$A1[0]		# a[6]*a[5]+t[7]
	 mov	$ai,%rax
	adc	%rdx,$A1[1]

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	 lea	32($j),$j
	adc	\$0,$A0[1]
	mul	$a0			# a[7]*a[4]
	add	%rax,$A0[0]		# a[7]*a[4]+a[6]*a[5]+t[6]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],-8($tptr,$j)	# t[7]

	cmp	\$0,$j
	jne	.Lsqr4x_1st

	xor	$A1[0],$A1[0]
	add	$A0[1],$A1[1]
	adc	\$0,$A1[0]
	mul	$a1			# a[7]*a[5]
	add	%rax,$A1[1]
	adc	%rdx,$A1[0]

	mov	$A1[1],($tptr)		# t[8]
	lea	16($i),$i
	mov	$A1[0],8($tptr)		# t[9]
	jmp	.Lsqr4x_outer

.align	16
.Lsqr4x_outer:				# comments apply to $num==6 case
	mov	-32($aptr,$i),$a0	# a[0]
	lea	64(%rsp,$num,2),$tptr	# end of tp[] buffer, &tp[2*$num]
	mov	-24($aptr,$i),%rax	# a[1]
	lea	-32($tptr,$i),$tptr	# end of tp[] window, &tp[2*$num-"$i"]
	mov	-16($aptr,$i),$ai	# a[2]
	mov	%rax,$a1

	mov	-24($tptr,$i),$A0[0]	# t[1]
	xor	$A0[1],$A0[1]
	mul	$a0			# a[1]*a[0]
	add	%rax,$A0[0]		# a[1]*a[0]+t[1]
	 mov	$ai,%rax		# a[2]
	adc	%rdx,$A0[1]
	mov	$A0[0],-24($tptr,$i)	# t[1]

	xor	$A0[0],$A0[0]
	add	-16($tptr,$i),$A0[1]	# a[2]*a[0]+t[2]
	adc	\$0,$A0[0]
	mul	$a0			# a[2]*a[0]
	add	%rax,$A0[1]
	 mov	$ai,%rax
	adc	%rdx,$A0[0]
	mov	$A0[1],-16($tptr,$i)	# t[2]

	lea	-16($i),$j		# j=-16
	xor	$A1[0],$A1[0]


	 mov	8($aptr,$j),$ai		# a[3]
	xor	$A1[1],$A1[1]
	add	8($tptr,$j),$A1[0]
	adc	\$0,$A1[1]
	mul	$a1			# a[2]*a[1]
	add	%rax,$A1[0]		# a[2]*a[1]+t[3]
	 mov	$ai,%rax
	adc	%rdx,$A1[1]

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	adc	\$0,$A0[1]
	mul	$a0			# a[3]*a[0]
	add	%rax,$A0[0]		# a[3]*a[0]+a[2]*a[1]+t[3]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],8($tptr,$j)	# t[3]

	lea	16($j),$j
	jmp	.Lsqr4x_inner

.align	16
.Lsqr4x_inner:
	 mov	($aptr,$j),$ai		# a[4]
	xor	$A1[0],$A1[0]
	add	($tptr,$j),$A1[1]
	adc	\$0,$A1[0]
	mul	$a1			# a[3]*a[1]
	add	%rax,$A1[1]		# a[3]*a[1]+t[4]
	 mov	$ai,%rax
	adc	%rdx,$A1[0]

	xor	$A0[0],$A0[0]
	add	$A1[1],$A0[1]
	adc	\$0,$A0[0]
	mul	$a0			# a[4]*a[0]
	add	%rax,$A0[1]		# a[4]*a[0]+a[3]*a[1]+t[4]
	 mov	$ai,%rax		# a[3]
	adc	%rdx,$A0[0]
	mov	$A0[1],($tptr,$j)	# t[4]

	 mov	8($aptr,$j),$ai		# a[5]
	xor	$A1[1],$A1[1]
	add	8($tptr,$j),$A1[0]
	adc	\$0,$A1[1]
	mul	$a1			# a[4]*a[3]
	add	%rax,$A1[0]		# a[4]*a[3]+t[5]
	 mov	$ai,%rax
	adc	%rdx,$A1[1]

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	lea	16($j),$j		# j++
	adc	\$0,$A0[1]
	mul	$a0			# a[5]*a[2]
	add	%rax,$A0[0]		# a[5]*a[2]+a[4]*a[3]+t[5]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],-8($tptr,$j)	# t[5], "preloaded t[1]" below

	cmp	\$0,$j
	jne	.Lsqr4x_inner

	xor	$A1[0],$A1[0]
	add	$A0[1],$A1[1]
	adc	\$0,$A1[0]
	mul	$a1			# a[5]*a[3]
	add	%rax,$A1[1]
	adc	%rdx,$A1[0]

	mov	$A1[1],($tptr)		# t[6], "preloaded t[2]" below
	mov	$A1[0],8($tptr)		# t[7], "preloaded t[3]" below

	add	\$16,$i
	jnz	.Lsqr4x_outer

					# comments apply to $num==4 case
	mov	-32($aptr),$a0		# a[0]
	lea	64(%rsp,$num,2),$tptr	# end of tp[] buffer, &tp[2*$num]
	mov	-24($aptr),%rax		# a[1]
	lea	-32($tptr,$i),$tptr	# end of tp[] window, &tp[2*$num-"$i"]
	mov	-16($aptr),$ai		# a[2]
	mov	%rax,$a1

	xor	$A0[1],$A0[1]
	mul	$a0			# a[1]*a[0]
	add	%rax,$A0[0]		# a[1]*a[0]+t[1], preloaded t[1]
	 mov	$ai,%rax		# a[2]
	adc	%rdx,$A0[1]
	mov	$A0[0],-24($tptr)	# t[1]

	xor	$A0[0],$A0[0]
	add	$A1[1],$A0[1]		# a[2]*a[0]+t[2], preloaded t[2]
	adc	\$0,$A0[0]
	mul	$a0			# a[2]*a[0]
	add	%rax,$A0[1]
	 mov	$ai,%rax
	adc	%rdx,$A0[0]
	mov	$A0[1],-16($tptr)	# t[2]

	 mov	-8($aptr),$ai		# a[3]
	mul	$a1			# a[2]*a[1]
	add	%rax,$A1[0]		# a[2]*a[1]+t[3], preloaded t[3]
	 mov	$ai,%rax
	adc	\$0,%rdx

	xor	$A0[1],$A0[1]
	add	$A1[0],$A0[0]
	 mov	%rdx,$A1[1]
	adc	\$0,$A0[1]
	mul	$a0			# a[3]*a[0]
	add	%rax,$A0[0]		# a[3]*a[0]+a[2]*a[1]+t[3]
	 mov	$ai,%rax
	adc	%rdx,$A0[1]
	mov	$A0[0],-8($tptr)	# t[3]

	xor	$A1[0],$A1[0]
	add	$A0[1],$A1[1]
	adc	\$0,$A1[0]
	mul	$a1			# a[3]*a[1]
	add	%rax,$A1[1]
	 mov	-16($aptr),%rax		# a[2]
	adc	%rdx,$A1[0]

	mov	$A1[1],($tptr)		# t[4]
	mov	$A1[0],8($tptr)		# t[5]

	mul	$ai			# a[2]*a[3]
___
{
my ($shift,$carry)=($a0,$a1);
my @S=(@A1,$ai,$n0);
$code.=<<___;
	 add	\$16,$i
	 xor	$shift,$shift
	 sub	$num,$i			# $i=16-$num
	 xor	$carry,$carry

	add	$A1[0],%rax		# t[5]
	adc	\$0,%rdx
	mov	%rax,8($tptr)		# t[5]
	mov	%rdx,16($tptr)		# t[6]
	mov	$carry,24($tptr)	# t[7]

	 mov	-16($aptr,$i),%rax	# a[0]
	lea	64(%rsp,$num,2),$tptr
	 xor	$A0[0],$A0[0]		# t[0]
	 mov	-24($tptr,$i,2),$A0[1]	# t[1]

	lea	($shift,$A0[0],2),$S[0]	# t[2*i]<<1 | shift
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[1]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[1]		# | t[2*i]>>63
	 mov	-16($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	-8($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[0]
	 mov	-8($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[0],-32($tptr,$i,2)
	adc	%rdx,$S[1]

	lea	($shift,$A0[0],2),$S[2]	# t[2*i]<<1 | shift
	 mov	$S[1],-24($tptr,$i,2)
	 sbb	$carry,$carry		# mov cf,$carry
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[3]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[3]		# | t[2*i]>>63
	 mov	0($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	8($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[2]
	 mov	0($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[2],-16($tptr,$i,2)
	adc	%rdx,$S[3]
	lea	16($i),$i
	mov	$S[3],-40($tptr,$i,2)
	sbb	$carry,$carry		# mov cf,$carry
	jmp	.Lsqr4x_shift_n_add

.align	16
.Lsqr4x_shift_n_add:
	lea	($shift,$A0[0],2),$S[0]	# t[2*i]<<1 | shift
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[1]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[1]		# | t[2*i]>>63
	 mov	-16($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	-8($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[0]
	 mov	-8($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[0],-32($tptr,$i,2)
	adc	%rdx,$S[1]

	lea	($shift,$A0[0],2),$S[2]	# t[2*i]<<1 | shift
	 mov	$S[1],-24($tptr,$i,2)
	 sbb	$carry,$carry		# mov cf,$carry
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[3]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[3]		# | t[2*i]>>63
	 mov	0($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	8($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[2]
	 mov	0($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[2],-16($tptr,$i,2)
	adc	%rdx,$S[3]

	lea	($shift,$A0[0],2),$S[0]	# t[2*i]<<1 | shift
	 mov	$S[3],-8($tptr,$i,2)
	 sbb	$carry,$carry		# mov cf,$carry
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[1]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[1]		# | t[2*i]>>63
	 mov	16($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	24($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[0]
	 mov	8($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[0],0($tptr,$i,2)
	adc	%rdx,$S[1]

	lea	($shift,$A0[0],2),$S[2]	# t[2*i]<<1 | shift
	 mov	$S[1],8($tptr,$i,2)
	 sbb	$carry,$carry		# mov cf,$carry
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[3]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[3]		# | t[2*i]>>63
	 mov	32($tptr,$i,2),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	40($tptr,$i,2),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[2]
	 mov	16($aptr,$i),%rax	# a[i+1]	# prefetch
	mov	$S[2],16($tptr,$i,2)
	adc	%rdx,$S[3]
	mov	$S[3],24($tptr,$i,2)
	sbb	$carry,$carry		# mov cf,$carry
	add	\$32,$i
	jnz	.Lsqr4x_shift_n_add

	lea	($shift,$A0[0],2),$S[0]	# t[2*i]<<1 | shift
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[1]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[1]		# | t[2*i]>>63
	 mov	-16($tptr),$A0[0]	# t[2*i+2]	# prefetch
	mov	$A0[1],$shift		# shift=t[2*i+1]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	 mov	-8($tptr),$A0[1]	# t[2*i+2+1]	# prefetch
	adc	%rax,$S[0]
	 mov	-8($aptr),%rax		# a[i+1]	# prefetch
	mov	$S[0],-32($tptr)
	adc	%rdx,$S[1]

	lea	($shift,$A0[0],2),$S[2]	# t[2*i]<<1|shift
	 mov	$S[1],-24($tptr)
	 sbb	$carry,$carry		# mov cf,$carry
	shr	\$63,$A0[0]
	lea	($j,$A0[1],2),$S[3]	# t[2*i+1]<<1 |
	shr	\$63,$A0[1]
	or	$A0[0],$S[3]		# | t[2*i]>>63
	mul	%rax			# a[i]*a[i]
	neg	$carry			# mov $carry,cf
	adc	%rax,$S[2]
	adc	%rdx,$S[3]
	mov	$S[2],-16($tptr)
	mov	$S[3],-8($tptr)
___
}
##############################################################
# Montgomery reduction part, "word-by-word" algorithm.
#
{
my ($topbit,$nptr)=("%rbp",$aptr);
my ($m0,$m1)=($a0,$a1);
my @Ni=("%rbx","%r9");
$code.=<<___;
	mov	40(%rsp),$nptr		# restore $nptr
	mov	48(%rsp),$n0		# restore *n0
	xor	$j,$j
	mov	$num,0(%rsp)		# save $num
	sub	$num,$j			# $j=-$num
	 mov	64(%rsp),$A0[0]		# t[0]		# modsched #
	 mov	$n0,$m0			#		# modsched #
	lea	64(%rsp,$num,2),%rax	# end of t[] buffer
	lea	64(%rsp,$num),$tptr	# end of t[] window
	mov	%rax,8(%rsp)		# save end of t[] buffer
	lea	($nptr,$num),$nptr	# end of n[] buffer
	xor	$topbit,$topbit		# $topbit=0

	mov	0($nptr,$j),%rax	# n[0]		# modsched #
	mov	8($nptr,$j),$Ni[1]	# n[1]		# modsched #
	 imulq	$A0[0],$m0		# m0=t[0]*n0	# modsched #
	 mov	%rax,$Ni[0]		#		# modsched #
	jmp	.Lsqr4x_mont_outer

.align	16
.Lsqr4x_mont_outer:
	xor	$A0[1],$A0[1]
	mul	$m0			# n[0]*m0
	add	%rax,$A0[0]		# n[0]*m0+t[0]
	 mov	$Ni[1],%rax
	adc	%rdx,$A0[1]
	mov	$n0,$m1

	xor	$A0[0],$A0[0]
	add	8($tptr,$j),$A0[1]
	adc	\$0,$A0[0]
	mul	$m0			# n[1]*m0
	add	%rax,$A0[1]		# n[1]*m0+t[1]
	 mov	$Ni[0],%rax
	adc	%rdx,$A0[0]

	imulq	$A0[1],$m1

	mov	16($nptr,$j),$Ni[0]	# n[2]
	xor	$A1[1],$A1[1]
	add	$A0[1],$A1[0]
	adc	\$0,$A1[1]
	mul	$m1			# n[0]*m1
	add	%rax,$A1[0]		# n[0]*m1+"t[1]"
	 mov	$Ni[0],%rax
	adc	%rdx,$A1[1]
	mov	$A1[0],8($tptr,$j)	# "t[1]"

	xor	$A0[1],$A0[1]
	add	16($tptr,$j),$A0[0]
	adc	\$0,$A0[1]
	mul	$m0			# n[2]*m0
	add	%rax,$A0[0]		# n[2]*m0+t[2]
	 mov	$Ni[1],%rax
	adc	%rdx,$A0[1]

	mov	24($nptr,$j),$Ni[1]	# n[3]
	xor	$A1[0],$A1[0]
	add	$A0[0],$A1[1]
	adc	\$0,$A1[0]
	mul	$m1			# n[1]*m1
	add	%rax,$A1[1]		# n[1]*m1+"t[2]"
	 mov	$Ni[1],%rax
	adc	%rdx,$A1[0]
	mov	$A1[1],16($tptr,$j)	# "t[2]"

	xor	$A0[0],$A0[0]
	add	24($tptr,$j),$A0[1]
	lea	32($j),$j
	adc	\$0,$A0[0]
	mul	$m0			# n[3]*m0
	add	%rax,$A0[1]		# n[3]*m0+t[3]
	 mov	$Ni[0],%rax
	adc	%rdx,$A0[0]
	jmp	.Lsqr4x_mont_inner

.align	16
.Lsqr4x_mont_inner:
	mov	($nptr,$j),$Ni[0]	# n[4]
	xor	$A1[1],$A1[1]
	add	$A0[1],$A1[0]
	adc	\$0,$A1[1]
	mul	$m1			# n[2]*m1
	add	%rax,$A1[0]		# n[2]*m1+"t[3]"
	 mov	$Ni[0],%rax
	adc	%rdx,$A1[1]
	mov	$A1[0],-8($tptr,$j)	# "t[3]"

	xor	$A0[1],$A0[1]
	add	($tptr,$j),$A0[0]
	adc	\$0,$A0[1]
	mul	$m0			# n[4]*m0
	add	%rax,$A0[0]		# n[4]*m0+t[4]
	 mov	$Ni[1],%rax
	adc	%rdx,$A0[1]

	mov	8($nptr,$j),$Ni[1]	# n[5]
	xor	$A1[0],$A1[0]
	add	$A0[0],$A1[1]
	adc	\$0,$A1[0]
	mul	$m1			# n[3]*m1
	add	%rax,$A1[1]		# n[3]*m1+"t[4]"
	 mov	$Ni[1],%rax
	adc	%rdx,$A1[0]
	mov	$A1[1],($tptr,$j)	# "t[4]"

	xor	$A0[0],$A0[0]
	add	8($tptr,$j),$A0[1]
	adc	\$0,$A0[0]
	mul	$m0			# n[5]*m0
	add	%rax,$A0[1]		# n[5]*m0+t[5]
	 mov	$Ni[0],%rax
	adc	%rdx,$A0[0]


	mov	16($nptr,$j),$Ni[0]	# n[6]
	xor	$A1[1],$A1[1]
	add	$A0[1],$A1[0]
	adc	\$0,$A1[1]
	mul	$m1			# n[4]*m1
	add	%rax,$A1[0]		# n[4]*m1+"t[5]"
	 mov	$Ni[0],%rax
	adc	%rdx,$A1[1]
	mov	$A1[0],8($tptr,$j)	# "t[5]"

	xor	$A0[1],$A0[1]
	add	16($tptr,$j),$A0[0]
	adc	\$0,$A0[1]
	mul	$m0			# n[6]*m0
	add	%rax,$A0[0]		# n[6]*m0+t[6]
	 mov	$Ni[1],%rax
	adc	%rdx,$A0[1]

	mov	24($nptr,$j),$Ni[1]	# n[7]
	xor	$A1[0],$A1[0]
	add	$A0[0],$A1[1]
	adc	\$0,$A1[0]
	mul	$m1			# n[5]*m1
	add	%rax,$A1[1]		# n[5]*m1+"t[6]"
	 mov	$Ni[1],%rax
	adc	%rdx,$A1[0]
	mov	$A1[1],16($tptr,$j)	# "t[6]"

	xor	$A0[0],$A0[0]
	add	24($tptr,$j),$A0[1]
	lea	32($j),$j
	adc	\$0,$A0[0]
	mul	$m0			# n[7]*m0
	add	%rax,$A0[1]		# n[7]*m0+t[7]
	 mov	$Ni[0],%rax
	adc	%rdx,$A0[0]
	cmp	\$0,$j
	jne	.Lsqr4x_mont_inner

	 sub	0(%rsp),$j		# $j=-$num	# modsched #
	 mov	$n0,$m0			#		# modsched #

	xor	$A1[1],$A1[1]
	add	$A0[1],$A1[0]
	adc	\$0,$A1[1]
	mul	$m1			# n[6]*m1
	add	%rax,$A1[0]		# n[6]*m1+"t[7]"
	mov	$Ni[1],%rax
	adc	%rdx,$A1[1]
	mov	$A1[0],-8($tptr)	# "t[7]"

	xor	$A0[1],$A0[1]
	add	($tptr),$A0[0]		# +t[8]
	adc	\$0,$A0[1]
	 mov	0($nptr,$j),$Ni[0]	# n[0]		# modsched #
	add	$topbit,$A0[0]
	adc	\$0,$A0[1]

	 imulq	16($tptr,$j),$m0	# m0=t[0]*n0	# modsched #
	xor	$A1[0],$A1[0]
	 mov	8($nptr,$j),$Ni[1]	# n[1]		# modsched #
	add	$A0[0],$A1[1]
	 mov	16($tptr,$j),$A0[0]	# t[0]		# modsched #
	adc	\$0,$A1[0]
	mul	$m1			# n[7]*m1
	add	%rax,$A1[1]		# n[7]*m1+"t[8]"
	 mov	$Ni[0],%rax		#		# modsched #
	adc	%rdx,$A1[0]
	mov	$A1[1],($tptr)		# "t[8]"

	xor	$topbit,$topbit
	add	8($tptr),$A1[0]		# +t[9]
	adc	$topbit,$topbit
	add	$A0[1],$A1[0]
	lea	16($tptr),$tptr		# "t[$num]>>128"
	adc	\$0,$topbit
	mov	$A1[0],-8($tptr)	# "t[9]"
	cmp	8(%rsp),$tptr		# are we done?
	jb	.Lsqr4x_mont_outer

	mov	0(%rsp),$num		# restore $num
	mov	$topbit,($tptr)		# save $topbit
___
}
##############################################################
# Post-condition, 4x unrolled copy from bn_mul_mont
#
{
my ($tptr,$nptr)=("%rbx",$aptr);
my @ri=("%rax","%rdx","%r10","%r11");
$code.=<<___;
	mov	64(%rsp,$num),@ri[0]	# tp[0]
	lea	64(%rsp,$num),$tptr	# upper half of t[2*$num] holds result
	mov	40(%rsp),$nptr		# restore $nptr
	shr	\$5,$num		# num/4
	mov	8($tptr),@ri[1]		# t[1]
	xor	$i,$i			# i=0 and clear CF!

	mov	32(%rsp),$rptr		# restore $rptr
	sub	0($nptr),@ri[0]
	mov	16($tptr),@ri[2]	# t[2]
	mov	24($tptr),@ri[3]	# t[3]
	sbb	8($nptr),@ri[1]
	lea	-1($num),$j		# j=num/4-1
	jmp	.Lsqr4x_sub
.align	16
.Lsqr4x_sub:
	mov	@ri[0],0($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	mov	@ri[1],8($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	16($nptr,$i,8),@ri[2]
	mov	32($tptr,$i,8),@ri[0]	# tp[i+1]
	mov	40($tptr,$i,8),@ri[1]
	sbb	24($nptr,$i,8),@ri[3]
	mov	@ri[2],16($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	mov	@ri[3],24($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	32($nptr,$i,8),@ri[0]
	mov	48($tptr,$i,8),@ri[2]
	mov	56($tptr,$i,8),@ri[3]
	sbb	40($nptr,$i,8),@ri[1]
	lea	4($i),$i		# i++
	dec	$j			# doesn't affect CF!
	jnz	.Lsqr4x_sub

	mov	@ri[0],0($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	mov	32($tptr,$i,8),@ri[0]	# load overflow bit
	sbb	16($nptr,$i,8),@ri[2]
	mov	@ri[1],8($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	sbb	24($nptr,$i,8),@ri[3]
	mov	@ri[2],16($rptr,$i,8)	# rp[i]=tp[i]-np[i]

	sbb	\$0,@ri[0]		# handle upmost overflow bit
	mov	@ri[3],24($rptr,$i,8)	# rp[i]=tp[i]-np[i]
	xor	$i,$i			# i=0
	and	@ri[0],$tptr
	not	@ri[0]
	mov	$rptr,$nptr
	and	@ri[0],$nptr
	lea	-1($num),$j
	or	$nptr,$tptr		# tp=borrow?tp:rp

	pxor	%xmm0,%xmm0
	lea	64(%rsp,$num,8),$nptr
	movdqu	($tptr),%xmm1
	lea	($nptr,$num,8),$nptr
	movdqa	%xmm0,64(%rsp)		# zap lower half of temporary vector
	movdqa	%xmm0,($nptr)		# zap upper half of temporary vector
	movdqu	%xmm1,($rptr)
	jmp	.Lsqr4x_copy
.align	16
.Lsqr4x_copy:				# copy or in-place refresh
	movdqu	16($tptr,$i),%xmm2
	movdqu	32($tptr,$i),%xmm1
	movdqa	%xmm0,80(%rsp,$i)	# zap lower half of temporary vector
	movdqa	%xmm0,96(%rsp,$i)	# zap lower half of temporary vector
	movdqa	%xmm0,16($nptr,$i)	# zap upper half of temporary vector
	movdqa	%xmm0,32($nptr,$i)	# zap upper half of temporary vector
	movdqu	%xmm2,16($rptr,$i)
	movdqu	%xmm1,32($rptr,$i)
	lea	32($i),$i
	dec	$j
	jnz	.Lsqr4x_copy

	movdqu	16($tptr,$i),%xmm2
	movdqa	%xmm0,80(%rsp,$i)	# zap lower half of temporary vector
	movdqa	%xmm0,16($nptr,$i)	# zap upper half of temporary vector
	movdqu	%xmm2,16($rptr,$i)
___
}
$code.=<<___;
	mov	56(%rsp),%rsi		# restore %rsp
	mov	\$1,%rax
	mov	0(%rsi),%r15
	mov	8(%rsi),%r14
	mov	16(%rsi),%r13
	mov	24(%rsi),%r12
	mov	32(%rsi),%rbp
	mov	40(%rsi),%rbx
	lea	48(%rsi),%rsp
.Lsqr4x_epilogue:
	ret
.size	bn_sqr4x_mont,.-bn_sqr4x_mont
___
}}}

print $code;
close STDOUT;
