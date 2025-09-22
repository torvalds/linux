#!/usr/bin/env perl

# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# SHA1 block procedure for MIPS.

# Performance improvement is 30% on unaligned input. The "secret" is
# to deploy lwl/lwr pair to load unaligned input. One could have
# vectorized Xupdate on MIPSIII/IV, but the goal was to code MIPS32-
# compatible subroutine. There is room for minor optimization on
# little-endian platforms...

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
# - never ever touch $tp, "thread pointer", former $gp;
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
$flavour = shift; # supported flavours are o32,n32,64,nubi32,nubi64

if ($flavour =~ /64|n32/i) {
	$PTR_ADD="dadd";	# incidentally works even on n32
	$PTR_SUB="dsub";	# incidentally works even on n32
	$REG_S="sd";
	$REG_L="ld";
	$PTR_SLL="dsll";	# incidentally works even on n32
	$SZREG=8;
} else {
	$PTR_ADD="add";
	$PTR_SUB="sub";
	$REG_S="sw";
	$REG_L="lw";
	$PTR_SLL="sll";
	$SZREG=4;
}
#
# <appro@openssl.org>
#
######################################################################

$big_endian=(`echo MIPSEL | $ENV{CC} -E -P -`=~/MIPSEL/)?1:0;

for (@ARGV) {	$output=$_ if (/^\w[\w\-]*\.\w+$/);   }
open STDOUT,">$output";

if (!defined($big_endian))
            {   $big_endian=(unpack('L',pack('N',1))==1);   }

# offsets of the Most and Least Significant Bytes
$MSB=$big_endian?0:3;
$LSB=3&~$MSB;

@X=map("\$$_",(8..23));	# a4-a7,s0-s11

$ctx=$a0;
$inp=$a1;
$num=$a2;
$A="\$1";
$B="\$2";
$C="\$3";
$D="\$7";
$E="\$24";	@V=($A,$B,$C,$D,$E);
$t0="\$25";
$t1=$num;	# $num is offloaded to stack
$t2="\$30";	# fp
$K="\$31";	# ra

sub BODY_00_14 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___	if (!$big_endian);
	srl	$t0,@X[$i],24	# byte swap($i)
	srl	$t1,@X[$i],8
	andi	$t2,@X[$i],0xFF00
	sll	@X[$i],@X[$i],24
	andi	$t1,0xFF00
	sll	$t2,$t2,8
	or	@X[$i],$t0
	or	$t1,$t2
	or	@X[$i],$t1
___
$code.=<<___;
	 lwl	@X[$j],$j*4+$MSB($inp)
	sll	$t0,$a,5	# $i
	addu	$e,$K
	 lwr	@X[$j],$j*4+$LSB($inp)
	srl	$t1,$a,27
	addu	$e,$t0
	xor	$t0,$c,$d
	addu	$e,$t1
	sll	$t2,$b,30
	and	$t0,$b
	srl	$b,$b,2
	xor	$t0,$d
	addu	$e,@X[$i]
	or	$b,$t2
	addu	$e,$t0
___
}

sub BODY_15_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;

$code.=<<___	if (!$big_endian && $i==15);
	srl	$t0,@X[$i],24	# byte swap($i)
	srl	$t1,@X[$i],8
	andi	$t2,@X[$i],0xFF00
	sll	@X[$i],@X[$i],24
	andi	$t1,0xFF00
	sll	$t2,$t2,8
	or	@X[$i],$t0
	or	@X[$i],$t1
	or	@X[$i],$t2
___
$code.=<<___;
	 xor	@X[$j%16],@X[($j+2)%16]
	sll	$t0,$a,5	# $i
	addu	$e,$K
	srl	$t1,$a,27
	addu	$e,$t0
	 xor	@X[$j%16],@X[($j+8)%16]
	xor	$t0,$c,$d
	addu	$e,$t1
	 xor	@X[$j%16],@X[($j+13)%16]
	sll	$t2,$b,30
	and	$t0,$b
	 srl	$t1,@X[$j%16],31
	 addu	@X[$j%16],@X[$j%16]
	srl	$b,$b,2
	xor	$t0,$d
	 or	@X[$j%16],$t1
	addu	$e,@X[$i%16]
	or	$b,$t2
	addu	$e,$t0
___
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___ if ($i<79);
	 xor	@X[$j%16],@X[($j+2)%16]
	sll	$t0,$a,5	# $i
	addu	$e,$K
	srl	$t1,$a,27
	addu	$e,$t0
	 xor	@X[$j%16],@X[($j+8)%16]
	xor	$t0,$c,$d
	addu	$e,$t1
	 xor	@X[$j%16],@X[($j+13)%16]
	sll	$t2,$b,30
	xor	$t0,$b
	 srl	$t1,@X[$j%16],31
	 addu	@X[$j%16],@X[$j%16]
	srl	$b,$b,2
	addu	$e,@X[$i%16]
	 or	@X[$j%16],$t1
	or	$b,$t2
	addu	$e,$t0
___
$code.=<<___ if ($i==79);
	 lw	@X[0],0($ctx)
	sll	$t0,$a,5	# $i
	addu	$e,$K
	 lw	@X[1],4($ctx)
	srl	$t1,$a,27
	addu	$e,$t0
	 lw	@X[2],8($ctx)
	xor	$t0,$c,$d
	addu	$e,$t1
	 lw	@X[3],12($ctx)
	sll	$t2,$b,30
	xor	$t0,$b
	 lw	@X[4],16($ctx)
	srl	$b,$b,2
	addu	$e,@X[$i%16]
	or	$b,$t2
	addu	$e,$t0
___
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___ if ($i<79);
	 xor	@X[$j%16],@X[($j+2)%16]
	sll	$t0,$a,5	# $i
	addu	$e,$K
	srl	$t1,$a,27
	addu	$e,$t0
	 xor	@X[$j%16],@X[($j+8)%16]
	and	$t0,$c,$d
	addu	$e,$t1
	 xor	@X[$j%16],@X[($j+13)%16]
	sll	$t2,$b,30
	addu	$e,$t0
	 srl	$t1,@X[$j%16],31
	xor	$t0,$c,$d
	 addu	@X[$j%16],@X[$j%16]
	and	$t0,$b
	srl	$b,$b,2
	 or	@X[$j%16],$t1
	addu	$e,@X[$i%16]
	or	$b,$t2
	addu	$e,$t0
___
}

$FRAMESIZE=16;	# large enough to accommodate NUBI saved registers
$SAVED_REGS_MASK = ($flavour =~ /nubi/i) ? 0xc0fff008 : 0xc0ff0000;

$code=<<___;
.text

.set	noat
.set	noreorder
.align	5
.globl	sha1_block_data_order
.ent	sha1_block_data_order
sha1_block_data_order:
	.frame	$sp,$FRAMESIZE*$SZREG,$ra
	.mask	$SAVED_REGS_MASK,-$SZREG
	.set	noreorder
	$PTR_SUB $sp,$FRAMESIZE*$SZREG
	$REG_S	$ra,($FRAMESIZE-1)*$SZREG($sp)
	$REG_S	$fp,($FRAMESIZE-2)*$SZREG($sp)
	$REG_S	$s11,($FRAMESIZE-3)*$SZREG($sp)
	$REG_S	$s10,($FRAMESIZE-4)*$SZREG($sp)
	$REG_S	$s9,($FRAMESIZE-5)*$SZREG($sp)
	$REG_S	$s8,($FRAMESIZE-6)*$SZREG($sp)
	$REG_S	$s7,($FRAMESIZE-7)*$SZREG($sp)
	$REG_S	$s6,($FRAMESIZE-8)*$SZREG($sp)
	$REG_S	$s5,($FRAMESIZE-9)*$SZREG($sp)
	$REG_S	$s4,($FRAMESIZE-10)*$SZREG($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);	# optimize non-nubi prologue
	$REG_S	$s3,($FRAMESIZE-11)*$SZREG($sp)
	$REG_S	$s2,($FRAMESIZE-12)*$SZREG($sp)
	$REG_S	$s1,($FRAMESIZE-13)*$SZREG($sp)
	$REG_S	$s0,($FRAMESIZE-14)*$SZREG($sp)
	$REG_S	$gp,($FRAMESIZE-15)*$SZREG($sp)
___
$code.=<<___;
	$PTR_SLL $num,6
	$PTR_ADD $num,$inp
	$REG_S	$num,0($sp)
	lw	$A,0($ctx)
	lw	$B,4($ctx)
	lw	$C,8($ctx)
	lw	$D,12($ctx)
	b	.Loop
	lw	$E,16($ctx)
.align	4
.Loop:
	.set	reorder
	lwl	@X[0],$MSB($inp)
	lui	$K,0x5a82
	lwr	@X[0],$LSB($inp)
	ori	$K,0x7999	# K_00_19
___
for ($i=0;$i<15;$i++)	{ &BODY_00_14($i,@V); unshift(@V,pop(@V)); }
for (;$i<20;$i++)	{ &BODY_15_19($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	lui	$K,0x6ed9
	ori	$K,0xeba1	# K_20_39
___
for (;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	lui	$K,0x8f1b
	ori	$K,0xbcdc	# K_40_59
___
for (;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	lui	$K,0xca62
	ori	$K,0xc1d6	# K_60_79
___
for (;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	$PTR_ADD $inp,64
	$REG_L	$num,0($sp)

	addu	$A,$X[0]
	addu	$B,$X[1]
	sw	$A,0($ctx)
	addu	$C,$X[2]
	addu	$D,$X[3]
	sw	$B,4($ctx)
	addu	$E,$X[4]
	sw	$C,8($ctx)
	sw	$D,12($ctx)
	sw	$E,16($ctx)
	.set	noreorder
	bne	$inp,$num,.Loop
	nop

	.set	noreorder
	$REG_L	$ra,($FRAMESIZE-1)*$SZREG($sp)
	$REG_L	$fp,($FRAMESIZE-2)*$SZREG($sp)
	$REG_L	$s11,($FRAMESIZE-3)*$SZREG($sp)
	$REG_L	$s10,($FRAMESIZE-4)*$SZREG($sp)
	$REG_L	$s9,($FRAMESIZE-5)*$SZREG($sp)
	$REG_L	$s8,($FRAMESIZE-6)*$SZREG($sp)
	$REG_L	$s7,($FRAMESIZE-7)*$SZREG($sp)
	$REG_L	$s6,($FRAMESIZE-8)*$SZREG($sp)
	$REG_L	$s5,($FRAMESIZE-9)*$SZREG($sp)
	$REG_L	$s4,($FRAMESIZE-10)*$SZREG($sp)
___
$code.=<<___ if ($flavour =~ /nubi/i);
	$REG_L	$s3,($FRAMESIZE-11)*$SZREG($sp)
	$REG_L	$s2,($FRAMESIZE-12)*$SZREG($sp)
	$REG_L	$s1,($FRAMESIZE-13)*$SZREG($sp)
	$REG_L	$s0,($FRAMESIZE-14)*$SZREG($sp)
	$REG_L	$gp,($FRAMESIZE-15)*$SZREG($sp)
___
$code.=<<___;
	jr	$ra
	$PTR_ADD $sp,$FRAMESIZE*$SZREG
.end	sha1_block_data_order
.rdata
.asciiz	"SHA1 for MIPS, CRYPTOGAMS by <appro\@openssl.org>"
___
print $code;
close STDOUT;
