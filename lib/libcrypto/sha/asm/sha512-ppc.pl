#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# I let hardware handle unaligned input, except on page boundaries
# (see below for details). Otherwise straightforward implementation
# with X vector in register bank. The module is big-endian [which is
# not big deal as there're no little-endian targets left around].

#			sha256		|	sha512
# 			-m64	-m32	|	-m64	-m32
# --------------------------------------+-----------------------
# PPC970,gcc-4.0.0	+50%	+38%	|	+40%	+410%(*)
# Power6,xlc-7		+150%	+90%	|	+100%	+430%(*)
#
# (*)	64-bit code in 32-bit application context, which actually is
#	on TODO list. It should be noted that for safe deployment in
#	32-bit *multi-threaded* context asynchronous signals should be
#	blocked upon entry to SHA512 block routine. This is because
#	32-bit signaling procedure invalidates upper halves of GPRs.
#	Context switch procedure preserves them, but not signaling:-(

# Second version is true multi-thread safe. Trouble with the original
# version was that it was using thread local storage pointer register.
# Well, it scrupulously preserved it, but the problem would arise the
# moment asynchronous signal was delivered and signal handler would
# dereference the TLS pointer. While it's never the case in openssl
# application or test suite, we have to respect this scenario and not
# use TLS pointer register. Alternative would be to require caller to
# block signals prior calling this routine. For the record, in 32-bit
# context R2 serves as TLS pointer, while in 64-bit context - R13.

$flavour=shift;
$output =shift;

if ($flavour =~ /64/) {
	$SIZE_T=8;
	$LRSAVE=2*$SIZE_T;
	$STU="stdu";
	$UCMP="cmpld";
	$SHL="sldi";
	$POP="ld";
	$PUSH="std";
} elsif ($flavour =~ /32/) {
	$SIZE_T=4;
	$LRSAVE=$SIZE_T;
	$STU="stwu";
	$UCMP="cmplw";
	$SHL="slwi";
	$POP="lwz";
	$PUSH="stw";
} else { die "nonsense $flavour"; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}ppc-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/ppc-xlate.pl" and -f $xlate) or
die "can't locate ppc-xlate.pl";

open STDOUT,"| $^X $xlate $flavour $output" || die "can't call $xlate: $!";

if ($output =~ /512/) {
	$func="sha512_block_data_order";
	$SZ=8;
	@Sigma0=(28,34,39);
	@Sigma1=(14,18,41);
	@sigma0=(1,  8, 7);
	@sigma1=(19,61, 6);
	$rounds=80;
	$LD="ld";
	$ST="std";
	$ROR="rotrdi";
	$SHR="srdi";
} else {
	$func="sha256_block_data_order";
	$SZ=4;
	@Sigma0=( 2,13,22);
	@Sigma1=( 6,11,25);
	@sigma0=( 7,18, 3);
	@sigma1=(17,19,10);
	$rounds=64;
	$LD="lwz";
	$ST="stw";
	$ROR="rotrwi";
	$SHR="srwi";
}

$FRAME=32*$SIZE_T+16*$SZ;
$LOCALS=6*$SIZE_T;

$sp ="r1";
$toc="r2";
$ctx="r3";	# zapped by $a0
$inp="r4";	# zapped by $a1
$num="r5";	# zapped by $t0

$T  ="r0";
$a0 ="r3";
$a1 ="r4";
$t0 ="r5";
$t1 ="r6";
$Tbl="r7";

$A  ="r8";
$B  ="r9";
$C  ="r10";
$D  ="r11";
$E  ="r12";
$F  ="r13";	$F="r2" if ($SIZE_T==8);# reassigned to exempt TLS pointer
$G  ="r14";
$H  ="r15";

@V=($A,$B,$C,$D,$E,$F,$G,$H);
@X=("r16","r17","r18","r19","r20","r21","r22","r23",
    "r24","r25","r26","r27","r28","r29","r30","r31");

$inp="r31";	# reassigned $inp! aliases with @X[15]

sub ROUND_00_15 {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;
$code.=<<___;
	$LD	$T,`$i*$SZ`($Tbl)
	$ROR	$a0,$e,$Sigma1[0]
	$ROR	$a1,$e,$Sigma1[1]
	and	$t0,$f,$e
	andc	$t1,$g,$e
	add	$T,$T,$h
	xor	$a0,$a0,$a1
	$ROR	$a1,$a1,`$Sigma1[2]-$Sigma1[1]`
	or	$t0,$t0,$t1		; Ch(e,f,g)
	add	$T,$T,@X[$i]
	xor	$a0,$a0,$a1		; Sigma1(e)
	add	$T,$T,$t0
	add	$T,$T,$a0

	$ROR	$a0,$a,$Sigma0[0]
	$ROR	$a1,$a,$Sigma0[1]
	and	$t0,$a,$b
	and	$t1,$a,$c
	xor	$a0,$a0,$a1
	$ROR	$a1,$a1,`$Sigma0[2]-$Sigma0[1]`
	xor	$t0,$t0,$t1
	and	$t1,$b,$c
	xor	$a0,$a0,$a1		; Sigma0(a)
	add	$d,$d,$T
	xor	$t0,$t0,$t1		; Maj(a,b,c)
	add	$h,$T,$a0
	add	$h,$h,$t0

___
}

sub ROUND_16_xx {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;
$i-=16;
$code.=<<___;
	$ROR	$a0,@X[($i+1)%16],$sigma0[0]
	$ROR	$a1,@X[($i+1)%16],$sigma0[1]
	$ROR	$t0,@X[($i+14)%16],$sigma1[0]
	$ROR	$t1,@X[($i+14)%16],$sigma1[1]
	xor	$a0,$a0,$a1
	$SHR	$a1,@X[($i+1)%16],$sigma0[2]
	xor	$t0,$t0,$t1
	$SHR	$t1,@X[($i+14)%16],$sigma1[2]
	add	@X[$i],@X[$i],@X[($i+9)%16]
	xor	$a0,$a0,$a1		; sigma0(X[(i+1)&0x0f])
	xor	$t0,$t0,$t1		; sigma1(X[(i+14)&0x0f])
	add	@X[$i],@X[$i],$a0
	add	@X[$i],@X[$i],$t0
___
&ROUND_00_15($i,$a,$b,$c,$d,$e,$f,$g,$h);
}

$code=<<___;
.machine	"any"
.text

.globl	$func
.align	6
$func:
	$STU	$sp,-$FRAME($sp)
	mflr	r0
	$SHL	$num,$num,`log(16*$SZ)/log(2)`

	$PUSH	$ctx,`$FRAME-$SIZE_T*22`($sp)

	$PUSH	$toc,`$FRAME-$SIZE_T*20`($sp)
	$PUSH	r13,`$FRAME-$SIZE_T*19`($sp)
	$PUSH	r14,`$FRAME-$SIZE_T*18`($sp)
	$PUSH	r15,`$FRAME-$SIZE_T*17`($sp)
	$PUSH	r16,`$FRAME-$SIZE_T*16`($sp)
	$PUSH	r17,`$FRAME-$SIZE_T*15`($sp)
	$PUSH	r18,`$FRAME-$SIZE_T*14`($sp)
	$PUSH	r19,`$FRAME-$SIZE_T*13`($sp)
	$PUSH	r20,`$FRAME-$SIZE_T*12`($sp)
	$PUSH	r21,`$FRAME-$SIZE_T*11`($sp)
	$PUSH	r22,`$FRAME-$SIZE_T*10`($sp)
	$PUSH	r23,`$FRAME-$SIZE_T*9`($sp)
	$PUSH	r24,`$FRAME-$SIZE_T*8`($sp)
	$PUSH	r25,`$FRAME-$SIZE_T*7`($sp)
	$PUSH	r26,`$FRAME-$SIZE_T*6`($sp)
	$PUSH	r27,`$FRAME-$SIZE_T*5`($sp)
	$PUSH	r28,`$FRAME-$SIZE_T*4`($sp)
	$PUSH	r29,`$FRAME-$SIZE_T*3`($sp)
	$PUSH	r30,`$FRAME-$SIZE_T*2`($sp)
	$PUSH	r31,`$FRAME-$SIZE_T*1`($sp)
	$PUSH	r0,`$FRAME+$LRSAVE`($sp)

	$LD	$A,`0*$SZ`($ctx)
	mr	$inp,r4				; incarnate $inp
	$LD	$B,`1*$SZ`($ctx)
	$LD	$C,`2*$SZ`($ctx)
	$LD	$D,`3*$SZ`($ctx)
	$LD	$E,`4*$SZ`($ctx)
	$LD	$F,`5*$SZ`($ctx)
	$LD	$G,`6*$SZ`($ctx)
	$LD	$H,`7*$SZ`($ctx)

	bcl	20,31,Lpc
Lpc:
	mflr	$Tbl
	addis	$Tbl,$Tbl,Ltable-Lpc\@ha
	addi	$Tbl,$Tbl,Ltable-Lpc\@l
	andi.	r0,$inp,3
	bne	Lunaligned
Laligned:
	add	$num,$inp,$num
	$PUSH	$num,`$FRAME-$SIZE_T*24`($sp)	; end pointer
	$PUSH	$inp,`$FRAME-$SIZE_T*23`($sp)	; inp pointer
	bl	Lsha2_block_private
	b	Ldone

; PowerPC specification allows an implementation to be ill-behaved
; upon unaligned access which crosses page boundary. "Better safe
; than sorry" principle makes me treat it specially. But I don't
; look for particular offending word, but rather for the input
; block which crosses the boundary. Once found that block is aligned
; and hashed separately...
.align	4
Lunaligned:
	subfic	$t1,$inp,4096
	andi.	$t1,$t1,`4096-16*$SZ`	; distance to closest page boundary
	beq	Lcross_page
	$UCMP	$num,$t1
	ble-	Laligned		; didn't cross the page boundary
	subfc	$num,$t1,$num
	add	$t1,$inp,$t1
	$PUSH	$num,`$FRAME-$SIZE_T*25`($sp)	; save real remaining num
	$PUSH	$t1,`$FRAME-$SIZE_T*24`($sp)	; intermediate end pointer
	$PUSH	$inp,`$FRAME-$SIZE_T*23`($sp)	; inp pointer
	bl	Lsha2_block_private
	; $inp equals to the intermediate end pointer here
	$POP	$num,`$FRAME-$SIZE_T*25`($sp)	; restore real remaining num
Lcross_page:
	li	$t1,`16*$SZ/4`
	mtctr	$t1
	addi	r20,$sp,$LOCALS			; aligned spot below the frame
Lmemcpy:
	lbz	r16,0($inp)
	lbz	r17,1($inp)
	lbz	r18,2($inp)
	lbz	r19,3($inp)
	addi	$inp,$inp,4
	stb	r16,0(r20)
	stb	r17,1(r20)
	stb	r18,2(r20)
	stb	r19,3(r20)
	addi	r20,r20,4
	bdnz	Lmemcpy

	$PUSH	$inp,`$FRAME-$SIZE_T*26`($sp)	; save real inp
	addi	$t1,$sp,`$LOCALS+16*$SZ`	; fictitious end pointer
	addi	$inp,$sp,$LOCALS		; fictitious inp pointer
	$PUSH	$num,`$FRAME-$SIZE_T*25`($sp)	; save real num
	$PUSH	$t1,`$FRAME-$SIZE_T*24`($sp)	; end pointer
	$PUSH	$inp,`$FRAME-$SIZE_T*23`($sp)	; inp pointer
	bl	Lsha2_block_private
	$POP	$inp,`$FRAME-$SIZE_T*26`($sp)	; restore real inp
	$POP	$num,`$FRAME-$SIZE_T*25`($sp)	; restore real num
	addic.	$num,$num,`-16*$SZ`		; num--
	bne-	Lunaligned

Ldone:
	$POP	r0,`$FRAME+$LRSAVE`($sp)
	$POP	$toc,`$FRAME-$SIZE_T*20`($sp)
	$POP	r13,`$FRAME-$SIZE_T*19`($sp)
	$POP	r14,`$FRAME-$SIZE_T*18`($sp)
	$POP	r15,`$FRAME-$SIZE_T*17`($sp)
	$POP	r16,`$FRAME-$SIZE_T*16`($sp)
	$POP	r17,`$FRAME-$SIZE_T*15`($sp)
	$POP	r18,`$FRAME-$SIZE_T*14`($sp)
	$POP	r19,`$FRAME-$SIZE_T*13`($sp)
	$POP	r20,`$FRAME-$SIZE_T*12`($sp)
	$POP	r21,`$FRAME-$SIZE_T*11`($sp)
	$POP	r22,`$FRAME-$SIZE_T*10`($sp)
	$POP	r23,`$FRAME-$SIZE_T*9`($sp)
	$POP	r24,`$FRAME-$SIZE_T*8`($sp)
	$POP	r25,`$FRAME-$SIZE_T*7`($sp)
	$POP	r26,`$FRAME-$SIZE_T*6`($sp)
	$POP	r27,`$FRAME-$SIZE_T*5`($sp)
	$POP	r28,`$FRAME-$SIZE_T*4`($sp)
	$POP	r29,`$FRAME-$SIZE_T*3`($sp)
	$POP	r30,`$FRAME-$SIZE_T*2`($sp)
	$POP	r31,`$FRAME-$SIZE_T*1`($sp)
	mtlr	r0
	addi	$sp,$sp,$FRAME
	blr

.align	4
Lsha2_block_private:
___
for($i=0;$i<16;$i++) {
$code.=<<___ if ($SZ==4);
	lwz	@X[$i],`$i*$SZ`($inp)
___
# 64-bit loads are split to 2x32-bit ones, as CPU can't handle
# unaligned 64-bit loads, only 32-bit ones...
$code.=<<___ if ($SZ==8);
	lwz	$t0,`$i*$SZ`($inp)
	lwz	@X[$i],`$i*$SZ+4`($inp)
	insrdi	@X[$i],$t0,32,0
___
	&ROUND_00_15($i,@V);
	unshift(@V,pop(@V));
}
$code.=<<___;
	li	$T,`$rounds/16-1`
	mtctr	$T
.align	4
Lrounds:
	addi	$Tbl,$Tbl,`16*$SZ`
___
for(;$i<32;$i++) {
	&ROUND_16_xx($i,@V);
	unshift(@V,pop(@V));
}
$code.=<<___;
	bdnz-	Lrounds

	$POP	$ctx,`$FRAME-$SIZE_T*22`($sp)
	$POP	$inp,`$FRAME-$SIZE_T*23`($sp)	; inp pointer
	$POP	$num,`$FRAME-$SIZE_T*24`($sp)	; end pointer
	subi	$Tbl,$Tbl,`($rounds-16)*$SZ`	; rewind Tbl

	$LD	r16,`0*$SZ`($ctx)
	$LD	r17,`1*$SZ`($ctx)
	$LD	r18,`2*$SZ`($ctx)
	$LD	r19,`3*$SZ`($ctx)
	$LD	r20,`4*$SZ`($ctx)
	$LD	r21,`5*$SZ`($ctx)
	$LD	r22,`6*$SZ`($ctx)
	addi	$inp,$inp,`16*$SZ`		; advance inp
	$LD	r23,`7*$SZ`($ctx)
	add	$A,$A,r16
	add	$B,$B,r17
	$PUSH	$inp,`$FRAME-$SIZE_T*23`($sp)
	add	$C,$C,r18
	$ST	$A,`0*$SZ`($ctx)
	add	$D,$D,r19
	$ST	$B,`1*$SZ`($ctx)
	add	$E,$E,r20
	$ST	$C,`2*$SZ`($ctx)
	add	$F,$F,r21
	$ST	$D,`3*$SZ`($ctx)
	add	$G,$G,r22
	$ST	$E,`4*$SZ`($ctx)
	add	$H,$H,r23
	$ST	$F,`5*$SZ`($ctx)
	$ST	$G,`6*$SZ`($ctx)
	$UCMP	$inp,$num
	$ST	$H,`7*$SZ`($ctx)
	bne	Lsha2_block_private
	blr
	.section .rodata
Ltable:
___
$code.=<<___ if ($SZ==8);
	.long	0x428a2f98,0xd728ae22,0x71374491,0x23ef65cd
	.long	0xb5c0fbcf,0xec4d3b2f,0xe9b5dba5,0x8189dbbc
	.long	0x3956c25b,0xf348b538,0x59f111f1,0xb605d019
	.long	0x923f82a4,0xaf194f9b,0xab1c5ed5,0xda6d8118
	.long	0xd807aa98,0xa3030242,0x12835b01,0x45706fbe
	.long	0x243185be,0x4ee4b28c,0x550c7dc3,0xd5ffb4e2
	.long	0x72be5d74,0xf27b896f,0x80deb1fe,0x3b1696b1
	.long	0x9bdc06a7,0x25c71235,0xc19bf174,0xcf692694
	.long	0xe49b69c1,0x9ef14ad2,0xefbe4786,0x384f25e3
	.long	0x0fc19dc6,0x8b8cd5b5,0x240ca1cc,0x77ac9c65
	.long	0x2de92c6f,0x592b0275,0x4a7484aa,0x6ea6e483
	.long	0x5cb0a9dc,0xbd41fbd4,0x76f988da,0x831153b5
	.long	0x983e5152,0xee66dfab,0xa831c66d,0x2db43210
	.long	0xb00327c8,0x98fb213f,0xbf597fc7,0xbeef0ee4
	.long	0xc6e00bf3,0x3da88fc2,0xd5a79147,0x930aa725
	.long	0x06ca6351,0xe003826f,0x14292967,0x0a0e6e70
	.long	0x27b70a85,0x46d22ffc,0x2e1b2138,0x5c26c926
	.long	0x4d2c6dfc,0x5ac42aed,0x53380d13,0x9d95b3df
	.long	0x650a7354,0x8baf63de,0x766a0abb,0x3c77b2a8
	.long	0x81c2c92e,0x47edaee6,0x92722c85,0x1482353b
	.long	0xa2bfe8a1,0x4cf10364,0xa81a664b,0xbc423001
	.long	0xc24b8b70,0xd0f89791,0xc76c51a3,0x0654be30
	.long	0xd192e819,0xd6ef5218,0xd6990624,0x5565a910
	.long	0xf40e3585,0x5771202a,0x106aa070,0x32bbd1b8
	.long	0x19a4c116,0xb8d2d0c8,0x1e376c08,0x5141ab53
	.long	0x2748774c,0xdf8eeb99,0x34b0bcb5,0xe19b48a8
	.long	0x391c0cb3,0xc5c95a63,0x4ed8aa4a,0xe3418acb
	.long	0x5b9cca4f,0x7763e373,0x682e6ff3,0xd6b2b8a3
	.long	0x748f82ee,0x5defb2fc,0x78a5636f,0x43172f60
	.long	0x84c87814,0xa1f0ab72,0x8cc70208,0x1a6439ec
	.long	0x90befffa,0x23631e28,0xa4506ceb,0xde82bde9
	.long	0xbef9a3f7,0xb2c67915,0xc67178f2,0xe372532b
	.long	0xca273ece,0xea26619c,0xd186b8c7,0x21c0c207
	.long	0xeada7dd6,0xcde0eb1e,0xf57d4f7f,0xee6ed178
	.long	0x06f067aa,0x72176fba,0x0a637dc5,0xa2c898a6
	.long	0x113f9804,0xbef90dae,0x1b710b35,0x131c471b
	.long	0x28db77f5,0x23047d84,0x32caab7b,0x40c72493
	.long	0x3c9ebe0a,0x15c9bebc,0x431d67c4,0x9c100d4c
	.long	0x4cc5d4be,0xcb3e42b6,0x597f299c,0xfc657e2a
	.long	0x5fcb6fab,0x3ad6faec,0x6c44198c,0x4a475817
___
$code.=<<___ if ($SZ==4);
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
print $code;
close STDOUT;
