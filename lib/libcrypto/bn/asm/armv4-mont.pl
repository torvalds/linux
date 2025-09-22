#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# January 2007.

# Montgomery multiplication for ARMv4.
#
# Performance improvement naturally varies among CPU implementations
# and compilers. The code was observed to provide +65-35% improvement
# [depending on key length, less for longer keys] on ARM920T, and
# +115-80% on Intel IXP425. This is compared to pre-bn_mul_mont code
# base and compiler generated code with in-lined umull and even umlal
# instructions. The latter means that this code didn't really have an 
# "advantage" of utilizing some "secret" instruction.
#
# The code is interoperable with Thumb ISA and is rather compact, less
# than 1/2KB. Windows CE port would be trivial, as it's exclusively
# about decorations, ABI and instruction syntax are identical.

while (($output=shift) && ($output!~/^\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

$num="r0";	# starts as num argument, but holds &tp[num-1]
$ap="r1";
$bp="r2"; $bi="r2"; $rp="r2";
$np="r3";
$tp="r4";
$aj="r5";
$nj="r6";
$tj="r7";
$n0="r8";
###########	# r9 is reserved by ELF as platform specific, e.g. TLS pointer
$alo="r10";	# sl, gcc uses it to keep @GOT
$ahi="r11";	# fp
$nlo="r12";	# ip
###########	# r13 is stack pointer
$nhi="r14";	# lr
###########	# r15 is program counter

#### argument block layout relative to &tp[num-1], a.k.a. $num
$_rp="$num,#12*4";
# ap permanently resides in r1
$_bp="$num,#13*4";
# np permanently resides in r3
$_n0="$num,#14*4";
$_num="$num,#15*4";	$_bpend=$_num;

$code=<<___;
.text

.global	bn_mul_mont
.type	bn_mul_mont,%function

.align	2
bn_mul_mont:
	stmdb	sp!,{r0,r2}		@ sp points at argument block
	ldr	$num,[sp,#3*4]		@ load num
	cmp	$num,#2
	movlt	r0,#0
	addlt	sp,sp,#2*4
	blt	.Labrt

	stmdb	sp!,{r4-r12,lr}		@ save 10 registers

	mov	$num,$num,lsl#2		@ rescale $num for byte count
	sub	sp,sp,$num		@ alloca(4*num)
	sub	sp,sp,#4		@ +extra dword
	sub	$num,$num,#4		@ "num=num-1"
	add	$tp,$bp,$num		@ &bp[num-1]

	add	$num,sp,$num		@ $num to point at &tp[num-1]
	ldr	$n0,[$_n0]		@ &n0
	ldr	$bi,[$bp]		@ bp[0]
	ldr	$aj,[$ap],#4		@ ap[0],ap++
	ldr	$nj,[$np],#4		@ np[0],np++
	ldr	$n0,[$n0]		@ *n0
	str	$tp,[$_bpend]		@ save &bp[num]

	umull	$alo,$ahi,$aj,$bi	@ ap[0]*bp[0]
	str	$n0,[$_n0]		@ save n0 value
	mul	$n0,$alo,$n0		@ "tp[0]"*n0
	mov	$nlo,#0
	umlal	$alo,$nlo,$nj,$n0	@ np[0]*n0+"t[0]"
	mov	$tp,sp

.L1st:
	ldr	$aj,[$ap],#4		@ ap[j],ap++
	mov	$alo,$ahi
	ldr	$nj,[$np],#4		@ np[j],np++
	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[j]*bp[0]
	mov	$nhi,#0
	umlal	$nlo,$nhi,$nj,$n0	@ np[j]*n0
	adds	$nlo,$nlo,$alo
	str	$nlo,[$tp],#4		@ tp[j-1]=,tp++
	adc	$nlo,$nhi,#0
	cmp	$tp,$num
	bne	.L1st

	adds	$nlo,$nlo,$ahi
	ldr	$tp,[$_bp]		@ restore bp
	mov	$nhi,#0
	ldr	$n0,[$_n0]		@ restore n0
	adc	$nhi,$nhi,#0
	str	$nlo,[$num]		@ tp[num-1]=
	str	$nhi,[$num,#4]		@ tp[num]=

.Louter:
	sub	$tj,$num,sp		@ "original" $num-1 value
	sub	$ap,$ap,$tj		@ "rewind" ap to &ap[1]
	ldr	$bi,[$tp,#4]!		@ *(++bp)
	sub	$np,$np,$tj		@ "rewind" np to &np[1]
	ldr	$aj,[$ap,#-4]		@ ap[0]
	ldr	$alo,[sp]		@ tp[0]
	ldr	$nj,[$np,#-4]		@ np[0]
	ldr	$tj,[sp,#4]		@ tp[1]

	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[0]*bp[i]+tp[0]
	str	$tp,[$_bp]		@ save bp
	mul	$n0,$alo,$n0
	mov	$nlo,#0
	umlal	$alo,$nlo,$nj,$n0	@ np[0]*n0+"tp[0]"
	mov	$tp,sp

.Linner:
	ldr	$aj,[$ap],#4		@ ap[j],ap++
	adds	$alo,$ahi,$tj		@ +=tp[j]
	ldr	$nj,[$np],#4		@ np[j],np++
	mov	$ahi,#0
	umlal	$alo,$ahi,$aj,$bi	@ ap[j]*bp[i]
	mov	$nhi,#0
	umlal	$nlo,$nhi,$nj,$n0	@ np[j]*n0
	adc	$ahi,$ahi,#0
	ldr	$tj,[$tp,#8]		@ tp[j+1]
	adds	$nlo,$nlo,$alo
	str	$nlo,[$tp],#4		@ tp[j-1]=,tp++
	adc	$nlo,$nhi,#0
	cmp	$tp,$num
	bne	.Linner

	adds	$nlo,$nlo,$ahi
	mov	$nhi,#0
	ldr	$tp,[$_bp]		@ restore bp
	adc	$nhi,$nhi,#0
	ldr	$n0,[$_n0]		@ restore n0
	adds	$nlo,$nlo,$tj
	ldr	$tj,[$_bpend]		@ restore &bp[num]
	adc	$nhi,$nhi,#0
	str	$nlo,[$num]		@ tp[num-1]=
	str	$nhi,[$num,#4]		@ tp[num]=

	cmp	$tp,$tj
	bne	.Louter

	ldr	$rp,[$_rp]		@ pull rp
	add	$num,$num,#4		@ $num to point at &tp[num]
	sub	$aj,$num,sp		@ "original" num value
	mov	$tp,sp			@ "rewind" $tp
	mov	$ap,$tp			@ "borrow" $ap
	sub	$np,$np,$aj		@ "rewind" $np to &np[0]

	subs	$tj,$tj,$tj		@ "clear" carry flag
.Lsub:	ldr	$tj,[$tp],#4
	ldr	$nj,[$np],#4
	sbcs	$tj,$tj,$nj		@ tp[j]-np[j]
	str	$tj,[$rp],#4		@ rp[j]=
	teq	$tp,$num		@ preserve carry
	bne	.Lsub
	sbcs	$nhi,$nhi,#0		@ upmost carry
	mov	$tp,sp			@ "rewind" $tp
	sub	$rp,$rp,$aj		@ "rewind" $rp

	and	$ap,$tp,$nhi
	bic	$np,$rp,$nhi
	orr	$ap,$ap,$np		@ ap=borrow?tp:rp

.Lcopy:	ldr	$tj,[$ap],#4		@ copy or in-place refresh
	str	sp,[$tp],#4		@ zap tp
	str	$tj,[$rp],#4
	cmp	$tp,$num
	bne	.Lcopy

	add	sp,$num,#4		@ skip over tp[num+1]
	ldmia	sp!,{r4-r12,lr}		@ restore registers
	add	sp,sp,#2*4		@ skip over {r0,r2}
	mov	r0,#1
.Labrt:	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	bx	lr			@ interoperable with Thumb ISA:-)
.size	bn_mul_mont,.-bn_mul_mont
.asciz	"Montgomery multiplication for ARMv4, CRYPTOGAMS by <appro\@openssl.org>"
.align	2
___

$code =~ s/\bbx\s+lr\b/.word\t0xe12fff1e/gm;	# make it possible to compile with -march=armv4
print $code;
close STDOUT;
