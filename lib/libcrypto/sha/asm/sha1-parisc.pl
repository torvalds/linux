#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# SHA1 block procedure for PA-RISC.

# June 2009.
#
# On PA-7100LC performance is >30% better than gcc 3.2 generated code
# for aligned input and >50% better for unaligned. Compared to vendor
# compiler on PA-8600 it's almost 60% faster in 64-bit build and just
# few percent faster in 32-bit one (this for aligned input, data for
# unaligned input is not available).
#
# Special thanks to polarhome.com for providing HP-UX account.

$flavour = shift;
$output = shift;
open STDOUT,">$output";

if ($flavour =~ /64/) {
	$LEVEL		="2.0W";
	$SIZE_T		=8;
	$FRAME_MARKER	=80;
	$SAVED_RP	=16;
	$PUSH		="std";
	$PUSHMA		="std,ma";
	$POP		="ldd";
	$POPMB		="ldd,mb";
} else {
	$LEVEL		="1.0";
	$SIZE_T		=4;
	$FRAME_MARKER	=48;
	$SAVED_RP	=20;
	$PUSH		="stw";
	$PUSHMA		="stwm";
	$POP		="ldw";
	$POPMB		="ldwm";
}

$FRAME=14*$SIZE_T+$FRAME_MARKER;# 14 saved regs + frame marker
				#                 [+ argument transfer]
$ctx="%r26";		# arg0
$inp="%r25";		# arg1
$num="%r24";		# arg2

$t0="%r28";
$t1="%r29";
$K="%r31";

@X=("%r1", "%r2", "%r3", "%r4", "%r5", "%r6", "%r7", "%r8",
    "%r9", "%r10","%r11","%r12","%r13","%r14","%r15","%r16",$t0);

@V=($A,$B,$C,$D,$E)=("%r19","%r20","%r21","%r22","%r23");

sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___ if ($i<15);
	addl	$K,$e,$e	; $i
	shd	$a,$a,27,$t1
	addl	@X[$i],$e,$e
	and	$c,$b,$t0
	addl	$t1,$e,$e
	andcm	$d,$b,$t1
	shd	$b,$b,2,$b
	or	$t1,$t0,$t0
	addl	$t0,$e,$e
___
$code.=<<___ if ($i>=15);	# with forward Xupdate
	addl	$K,$e,$e	; $i
	shd	$a,$a,27,$t1
	xor	@X[($j+2)%16],@X[$j%16],@X[$j%16]
	addl	@X[$i%16],$e,$e
	and	$c,$b,$t0
	xor	@X[($j+8)%16],@X[$j%16],@X[$j%16]
	addl	$t1,$e,$e
	andcm	$d,$b,$t1
	shd	$b,$b,2,$b
	or	$t1,$t0,$t0
	xor	@X[($j+13)%16],@X[$j%16],@X[$j%16]
	add	$t0,$e,$e
	shd	@X[$j%16],@X[$j%16],31,@X[$j%16]
___
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___ if ($i<79);
	xor	@X[($j+2)%16],@X[$j%16],@X[$j%16]	; $i
	addl	$K,$e,$e
	shd	$a,$a,27,$t1
	xor	@X[($j+8)%16],@X[$j%16],@X[$j%16]
	addl	@X[$i%16],$e,$e
	xor	$b,$c,$t0
	xor	@X[($j+13)%16],@X[$j%16],@X[$j%16]
	addl	$t1,$e,$e
	shd	$b,$b,2,$b
	xor	$d,$t0,$t0
	shd	@X[$j%16],@X[$j%16],31,@X[$j%16]
	addl	$t0,$e,$e
___
$code.=<<___ if ($i==79);	# with context load
	ldw	0($ctx),@X[0]	; $i
	addl	$K,$e,$e
	shd	$a,$a,27,$t1
	ldw	4($ctx),@X[1]
	addl	@X[$i%16],$e,$e
	xor	$b,$c,$t0
	ldw	8($ctx),@X[2]
	addl	$t1,$e,$e
	shd	$b,$b,2,$b
	xor	$d,$t0,$t0
	ldw	12($ctx),@X[3]
	addl	$t0,$e,$e
	ldw	16($ctx),@X[4]
___
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___;
	shd	$a,$a,27,$t1	; $i
	addl	$K,$e,$e
	xor	@X[($j+2)%16],@X[$j%16],@X[$j%16]
	xor	$d,$c,$t0
	addl	@X[$i%16],$e,$e
	xor	@X[($j+8)%16],@X[$j%16],@X[$j%16]
	and	$b,$t0,$t0
	addl	$t1,$e,$e
	shd	$b,$b,2,$b
	xor	@X[($j+13)%16],@X[$j%16],@X[$j%16]
	addl	$t0,$e,$e
	and	$d,$c,$t1
	shd	@X[$j%16],@X[$j%16],31,@X[$j%16]
	addl	$t1,$e,$e
___
}

$code=<<___;
	.LEVEL	$LEVEL
	.text

	.EXPORT	sha1_block_data_order,ENTRY,ARGW0=GR,ARGW1=GR,ARGW2=GR
sha1_block_data_order
	.PROC
	.CALLINFO	FRAME=`$FRAME-14*$SIZE_T`,NO_CALLS,SAVE_RP,ENTRY_GR=16
	.ENTRY
	$PUSH	%r2,-$SAVED_RP(%sp)	; standard prologue
	$PUSHMA	%r3,$FRAME(%sp)
	$PUSH	%r4,`-$FRAME+1*$SIZE_T`(%sp)
	$PUSH	%r5,`-$FRAME+2*$SIZE_T`(%sp)
	$PUSH	%r6,`-$FRAME+3*$SIZE_T`(%sp)
	$PUSH	%r7,`-$FRAME+4*$SIZE_T`(%sp)
	$PUSH	%r8,`-$FRAME+5*$SIZE_T`(%sp)
	$PUSH	%r9,`-$FRAME+6*$SIZE_T`(%sp)
	$PUSH	%r10,`-$FRAME+7*$SIZE_T`(%sp)
	$PUSH	%r11,`-$FRAME+8*$SIZE_T`(%sp)
	$PUSH	%r12,`-$FRAME+9*$SIZE_T`(%sp)
	$PUSH	%r13,`-$FRAME+10*$SIZE_T`(%sp)
	$PUSH	%r14,`-$FRAME+11*$SIZE_T`(%sp)
	$PUSH	%r15,`-$FRAME+12*$SIZE_T`(%sp)
	$PUSH	%r16,`-$FRAME+13*$SIZE_T`(%sp)

	ldw	0($ctx),$A
	ldw	4($ctx),$B
	ldw	8($ctx),$C
	ldw	12($ctx),$D
	ldw	16($ctx),$E

	extru	$inp,31,2,$t0		; t0=inp&3;
	sh3addl	$t0,%r0,$t0		; t0*=8;
	subi	32,$t0,$t0		; t0=32-t0;
	mtctl	$t0,%cr11		; %sar=t0;

L\$oop
	ldi	3,$t0
	andcm	$inp,$t0,$t0		; 64-bit neutral
___
	for ($i=0;$i<15;$i++) {		# load input block
	$code.="\tldw	`4*$i`($t0),@X[$i]\n";		}
$code.=<<___;
	cmpb,*=	$inp,$t0,L\$aligned
	ldw	60($t0),@X[15]
	ldw	64($t0),@X[16]
___
	for ($i=0;$i<16;$i++) {		# align input
	$code.="\tvshd	@X[$i],@X[$i+1],@X[$i]\n";	}
$code.=<<___;
L\$aligned
	ldil	L'0x5a827000,$K		; K_00_19
	ldo	0x999($K),$K
___
for ($i=0;$i<20;$i++)   { &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	ldil	L'0x6ed9e000,$K		; K_20_39
	ldo	0xba1($K),$K
___

for (;$i<40;$i++)       { &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	ldil	L'0x8f1bb000,$K		; K_40_59
	ldo	0xcdc($K),$K
___

for (;$i<60;$i++)       { &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	ldil	L'0xca62c000,$K		; K_60_79
	ldo	0x1d6($K),$K
___
for (;$i<80;$i++)       { &BODY_20_39($i,@V); unshift(@V,pop(@V)); }

$code.=<<___;
	addl	@X[0],$A,$A
	addl	@X[1],$B,$B
	addl	@X[2],$C,$C
	addl	@X[3],$D,$D
	addl	@X[4],$E,$E
	stw	$A,0($ctx)
	stw	$B,4($ctx)
	stw	$C,8($ctx)
	stw	$D,12($ctx)
	stw	$E,16($ctx)
	addib,*<> -1,$num,L\$oop
	ldo	64($inp),$inp

	$POP	`-$FRAME-$SAVED_RP`(%sp),%r2	; standard epilogue
	$POP	`-$FRAME+1*$SIZE_T`(%sp),%r4
	$POP	`-$FRAME+2*$SIZE_T`(%sp),%r5
	$POP	`-$FRAME+3*$SIZE_T`(%sp),%r6
	$POP	`-$FRAME+4*$SIZE_T`(%sp),%r7
	$POP	`-$FRAME+5*$SIZE_T`(%sp),%r8
	$POP	`-$FRAME+6*$SIZE_T`(%sp),%r9
	$POP	`-$FRAME+7*$SIZE_T`(%sp),%r10
	$POP	`-$FRAME+8*$SIZE_T`(%sp),%r11
	$POP	`-$FRAME+9*$SIZE_T`(%sp),%r12
	$POP	`-$FRAME+10*$SIZE_T`(%sp),%r13
	$POP	`-$FRAME+11*$SIZE_T`(%sp),%r14
	$POP	`-$FRAME+12*$SIZE_T`(%sp),%r15
	$POP	`-$FRAME+13*$SIZE_T`(%sp),%r16
	bv	(%r2)
	.EXIT
	$POPMB	-$FRAME(%sp),%r3
	.PROCEND
___

$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/,\*/,/gm		if ($SIZE_T==4);
$code =~ s/\bbv\b/bve/gm	if ($SIZE_T==8);
print $code;
close STDOUT;
