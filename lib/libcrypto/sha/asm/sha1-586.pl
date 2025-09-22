#!/usr/bin/env perl

# ====================================================================
# [Re]written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# "[Re]written" was achieved in two major overhauls. In 2004 BODY_*
# functions were re-implemented to address P4 performance issue [see
# commentary below], and in 2006 the rest was rewritten in order to
# gain freedom to liberate licensing terms.

# January, September 2004.
#
# It was noted that Intel IA-32 C compiler generates code which
# performs ~30% *faster* on P4 CPU than original *hand-coded*
# SHA1 assembler implementation. To address this problem (and
# prove that humans are still better than machines:-), the
# original code was overhauled, which resulted in following
# performance changes:
#
#		compared with original	compared with Intel cc
#		assembler impl.		generated code
# Pentium	-16%			+48%
# PIII/AMD	+8%			+16%
# P4		+85%(!)			+45%
#
# As you can see Pentium came out as looser:-( Yet I reckoned that
# improvement on P4 outweighs the loss and incorporate this
# re-tuned code to 0.9.7 and later.
# ----------------------------------------------------------------
#					<appro@fy.chalmers.se>

# August 2009.
#
# George Spelvin has tipped that F_40_59(b,c,d) can be rewritten as
# '(c&d) + (b&(c^d))', which allows to accumulate partial results
# and lighten "pressure" on scratch registers. This resulted in
# >12% performance improvement on contemporary AMD cores (with no
# degradation on other CPUs:-). Also, the code was revised to maximize
# "distance" between instructions producing input to 'lea' instruction
# and the 'lea' instruction itself, which is essential for Intel Atom
# core and resulted in ~15% improvement.

# October 2010.
#
# Add SSSE3, Supplemental[!] SSE3, implementation. The idea behind it
# is to offload message schedule denoted by Wt in NIST specification,
# or Xupdate in OpenSSL source, to SIMD unit. The idea is not novel,
# and in SSE2 context was first explored by Dean Gaudet in 2004, see
# http://arctic.org/~dean/crypto/sha1.html. Since then several things
# have changed that made it interesting again:
#
# a) XMM units became faster and wider;
# b) instruction set became more versatile;
# c) an important observation was made by Max Locktykhin, which made
#    it possible to reduce amount of instructions required to perform
#    the operation in question, for further details see
#    http://software.intel.com/en-us/articles/improving-the-performance-of-the-secure-hash-algorithm-1/.

# April 2011.
#
# Add AVX code path, probably most controversial... The thing is that
# switch to AVX alone improves performance by as little as 4% in
# comparison to SSSE3 code path. But below result doesn't look like
# 4% improvement... Trouble is that Sandy Bridge decodes 'ro[rl]' as
# pair of µ-ops, and it's the additional µ-ops, two per round, that
# make it run slower than Core2 and Westmere. But 'sh[rl]d' is decoded
# as single µ-op by Sandy Bridge and it's replacing 'ro[rl]' with
# equivalent 'sh[rl]d' that is responsible for the impressive 5.1
# cycles per processed byte. But 'sh[rl]d' is not something that used
# to be fast, nor does it appear to be fast in upcoming Bulldozer
# [according to its optimization manual]. Which is why AVX code path
# is guarded by *both* AVX and synthetic bit denoting Intel CPUs.
# One can argue that it's unfair to AMD, but without 'sh[rl]d' it
# makes no sense to keep the AVX code path. If somebody feels that
# strongly, it's probably more appropriate to discuss possibility of
# using vector rotate XOP on AMD...

######################################################################
# Current performance is summarized in following table. Numbers are
# CPU clock cycles spent to process single byte (less is better).
#
#		x86		SSSE3		AVX
# Pentium	15.7		-
# PIII		11.5		-
# P4		10.6		-
# AMD K8	7.1		-
# Core2		7.3		6.1/+20%	-
# Atom		12.5		9.5(*)/+32%	-
# Westmere	7.3		5.6/+30%	-
# Sandy Bridge	8.8		6.2/+40%	5.1(**)/+70%
#
# (*)	Loop is 1056 instructions long and expected result is ~8.25.
#	It remains mystery [to me] why ILP is limited to 1.7.
#
# (**)	As per above comment, the result is for AVX *plus* sh[rl]d.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

&asm_init($ARGV[0],"sha1-586.pl",$ARGV[$#ARGV] eq "386");

$xmm=$ymm=1;

&external_label("OPENSSL_ia32cap_P") if ($xmm);


$A="eax";
$B="ebx";
$C="ecx";
$D="edx";
$E="edi";
$T="esi";
$tmp1="ebp";

@V=($A,$B,$C,$D,$E,$T);

$alt=0;	# 1 denotes alternative IALU implementation, which performs
	# 8% *worse* on P4, same on Westmere and Atom, 2% better on
	# Sandy Bridge...

sub BODY_00_15
	{
	local($n,$a,$b,$c,$d,$e,$f)=@_;

	&comment("00_15 $n");

	&mov($f,$c);			# f to hold F_00_19(b,c,d)
	 if ($n==0)  { &mov($tmp1,$a); }
	 else        { &mov($a,$tmp1); }
	&rotl($tmp1,5);			# tmp1=ROTATE(a,5)
	 &xor($f,$d);
	&add($tmp1,$e);			# tmp1+=e;
	 &mov($e,&swtmp($n%16));	# e becomes volatile and is loaded
	 				# with xi, also note that e becomes
					# f in next round...
	&and($f,$b);
	&rotr($b,2);			# b=ROTATE(b,30)
	 &xor($f,$d);			# f holds F_00_19(b,c,d)
	&lea($tmp1,&DWP(0x5a827999,$tmp1,$e));	# tmp1+=K_00_19+xi

	if ($n==15) { &mov($e,&swtmp(($n+1)%16));# pre-fetch f for next round
		      &add($f,$tmp1); }	# f+=tmp1
	else        { &add($tmp1,$f); }	# f becomes a in next round
	&mov($tmp1,$a)			if ($alt && $n==15);
	}

sub BODY_16_19
	{
	local($n,$a,$b,$c,$d,$e,$f)=@_;

	&comment("16_19 $n");

if ($alt) {
	&xor($c,$d);
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&and($tmp1,$c);			# tmp1 to hold F_00_19(b,c,d), b&=c^d
	 &xor($f,&swtmp(($n+8)%16));
	&xor($tmp1,$d);			# tmp1=F_00_19(b,c,d)
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &add($e,$tmp1);		# e+=F_00_19(b,c,d)
	&xor($c,$d);			# restore $c
	 &mov($tmp1,$a);		# b in next round
	&rotr($b,$n==16?2:7);		# b=ROTATE(b,30)
	 &mov(&swtmp($n%16),$f);	# xi=f
	&rotl($a,5);			# ROTATE(a,5)
	 &lea($f,&DWP(0x5a827999,$f,$e));# f+=F_00_19(b,c,d)+e
	&mov($e,&swtmp(($n+1)%16));	# pre-fetch f for next round
	 &add($f,$a);			# f+=ROTATE(a,5)
} else {
	&mov($tmp1,$c);			# tmp1 to hold F_00_19(b,c,d)
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&xor($tmp1,$d);
	 &xor($f,&swtmp(($n+8)%16));
	&and($tmp1,$b);
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &xor($tmp1,$d);		# tmp1=F_00_19(b,c,d)
	&add($e,$tmp1);			# e+=F_00_19(b,c,d)
	 &mov($tmp1,$a);
	&rotr($b,2);			# b=ROTATE(b,30)
	 &mov(&swtmp($n%16),$f);	# xi=f
	&rotl($tmp1,5);			# ROTATE(a,5)
	 &lea($f,&DWP(0x5a827999,$f,$e));# f+=F_00_19(b,c,d)+e
	&mov($e,&swtmp(($n+1)%16));	# pre-fetch f for next round
	 &add($f,$tmp1);		# f+=ROTATE(a,5)
}
	}

sub BODY_20_39
	{
	local($n,$a,$b,$c,$d,$e,$f)=@_;
	local $K=($n<40)?0x6ed9eba1:0xca62c1d6;

	&comment("20_39 $n");

if ($alt) {
	&xor($tmp1,$c);			# tmp1 to hold F_20_39(b,c,d), b^=c
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&xor($tmp1,$d);			# tmp1 holds F_20_39(b,c,d)
	 &xor($f,&swtmp(($n+8)%16));
	&add($e,$tmp1);			# e+=F_20_39(b,c,d)
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &mov($tmp1,$a);		# b in next round
	&rotr($b,7);			# b=ROTATE(b,30)
	 &mov(&swtmp($n%16),$f)		if($n<77);# xi=f
	&rotl($a,5);			# ROTATE(a,5)
	 &xor($b,$c)			if($n==39);# warm up for BODY_40_59
	&and($tmp1,$b)			if($n==39);
	 &lea($f,&DWP($K,$f,$e));	# f+=e+K_XX_YY
	&mov($e,&swtmp(($n+1)%16))	if($n<79);# pre-fetch f for next round
	 &add($f,$a);			# f+=ROTATE(a,5)
	&rotr($a,5)			if ($n==79);
} else {
	&mov($tmp1,$b);			# tmp1 to hold F_20_39(b,c,d)
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&xor($tmp1,$c);
	 &xor($f,&swtmp(($n+8)%16));
	&xor($tmp1,$d);			# tmp1 holds F_20_39(b,c,d)
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &add($e,$tmp1);		# e+=F_20_39(b,c,d)
	&rotr($b,2);			# b=ROTATE(b,30)
	 &mov($tmp1,$a);
	&rotl($tmp1,5);			# ROTATE(a,5)
	 &mov(&swtmp($n%16),$f) if($n<77);# xi=f
	&lea($f,&DWP($K,$f,$e));	# f+=e+K_XX_YY
	 &mov($e,&swtmp(($n+1)%16)) if($n<79);# pre-fetch f for next round
	&add($f,$tmp1);			# f+=ROTATE(a,5)
}
	}

sub BODY_40_59
	{
	local($n,$a,$b,$c,$d,$e,$f)=@_;

	&comment("40_59 $n");

if ($alt) {
	&add($e,$tmp1);			# e+=b&(c^d)
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&mov($tmp1,$d);
	 &xor($f,&swtmp(($n+8)%16));
	&xor($c,$d);			# restore $c
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &and($tmp1,$c);
	&rotr($b,7);			# b=ROTATE(b,30)
	 &add($e,$tmp1);		# e+=c&d
	&mov($tmp1,$a);			# b in next round
	 &mov(&swtmp($n%16),$f);	# xi=f
	&rotl($a,5);			# ROTATE(a,5)
	 &xor($b,$c)			if ($n<59);
	&and($tmp1,$b)			if ($n<59);# tmp1 to hold F_40_59(b,c,d)
	 &lea($f,&DWP(0x8f1bbcdc,$f,$e));# f+=K_40_59+e+(b&(c^d))
	&mov($e,&swtmp(($n+1)%16));	# pre-fetch f for next round
	 &add($f,$a);			# f+=ROTATE(a,5)
} else {
	&mov($tmp1,$c);			# tmp1 to hold F_40_59(b,c,d)
	 &xor($f,&swtmp(($n+2)%16));	# f to hold Xupdate(xi,xa,xb,xc,xd)
	&xor($tmp1,$d);
	 &xor($f,&swtmp(($n+8)%16));
	&and($tmp1,$b);
	 &xor($f,&swtmp(($n+13)%16));	# f holds xa^xb^xc^xd
	&rotl($f,1);			# f=ROTATE(f,1)
	 &add($tmp1,$e);		# b&(c^d)+=e
	&rotr($b,2);			# b=ROTATE(b,30)
	 &mov($e,$a);			# e becomes volatile
	&rotl($e,5);			# ROTATE(a,5)
	 &mov(&swtmp($n%16),$f);	# xi=f
	&lea($f,&DWP(0x8f1bbcdc,$f,$tmp1));# f+=K_40_59+e+(b&(c^d))
	 &mov($tmp1,$c);
	&add($f,$e);			# f+=ROTATE(a,5)
	 &and($tmp1,$d);
	&mov($e,&swtmp(($n+1)%16));	# pre-fetch f for next round
	 &add($f,$tmp1);		# f+=c&d
}
	}

&function_begin("sha1_block_data_order");
if ($xmm) {
  &static_label("ssse3_shortcut");
  &static_label("avx_shortcut")		if ($ymm);
  &static_label("K_XX_XX");

	&picsetup($tmp1);
	&picsymbol($T, "OPENSSL_ia32cap_P", $tmp1);
	&picsymbol($tmp1, &label("K_XX_XX"), $tmp1);

	&mov	($A,&DWP(0,$T));
	&mov	($D,&DWP(4,$T));
	&test	($D,"\$IA32CAP_MASK1_SSSE3");	# check SSSE3 bit
	&jz	(&label("x86"));
	&test	($A,"\$IA32CAP_MASK0_FXSR");	# check FXSR bit
	&jz	(&label("x86"));
	if ($ymm) {
		&and	($D,"\$IA32CAP_MASK1_AVX");	# mask AVX bit
		&and	($A,"\$IA32CAP_MASK0_INTEL");	# mask "Intel CPU" bit
		&or	($A,$D);
		&cmp	($A,"\$(IA32CAP_MASK1_AVX | IA32CAP_MASK0_INTEL)");
		&je	(&label("avx_shortcut"));
	}
	&jmp	(&label("ssse3_shortcut"));
  &set_label("x86",16);
}
	&mov($tmp1,&wparam(0));	# SHA_CTX *c
	&mov($T,&wparam(1));	# const void *input
	&mov($A,&wparam(2));	# size_t num
	&stack_push(16+3);	# allocate X[16]
	&shl($A,6);
	&add($A,$T);
	&mov(&wparam(2),$A);	# pointer beyond the end of input
	&mov($E,&DWP(16,$tmp1));# pre-load E
	&jmp(&label("loop"));

&set_label("loop",16);

	# copy input chunk to X, but reversing byte order!
	for ($i=0; $i<16; $i+=4)
		{
		&mov($A,&DWP(4*($i+0),$T));
		&mov($B,&DWP(4*($i+1),$T));
		&mov($C,&DWP(4*($i+2),$T));
		&mov($D,&DWP(4*($i+3),$T));
		&bswap($A);
		&bswap($B);
		&bswap($C);
		&bswap($D);
		&mov(&swtmp($i+0),$A);
		&mov(&swtmp($i+1),$B);
		&mov(&swtmp($i+2),$C);
		&mov(&swtmp($i+3),$D);
		}
	&mov(&wparam(1),$T);	# redundant in 1st spin

	&mov($A,&DWP(0,$tmp1));	# load SHA_CTX
	&mov($B,&DWP(4,$tmp1));
	&mov($C,&DWP(8,$tmp1));
	&mov($D,&DWP(12,$tmp1));
	# E is pre-loaded

	for($i=0;$i<16;$i++)	{ &BODY_00_15($i,@V); unshift(@V,pop(@V)); }
	for(;$i<20;$i++)	{ &BODY_16_19($i,@V); unshift(@V,pop(@V)); }
	for(;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
	for(;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
	for(;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }

	(($V[5] eq $D) and ($V[0] eq $E)) or die;	# double-check

	&mov($tmp1,&wparam(0));	# re-load SHA_CTX*
	&mov($D,&wparam(1));	# D is last "T" and is discarded

	&add($E,&DWP(0,$tmp1));	# E is last "A"...
	&add($T,&DWP(4,$tmp1));
	&add($A,&DWP(8,$tmp1));
	&add($B,&DWP(12,$tmp1));
	&add($C,&DWP(16,$tmp1));

	&mov(&DWP(0,$tmp1),$E);	# update SHA_CTX
	 &add($D,64);		# advance input pointer
	&mov(&DWP(4,$tmp1),$T);
	 &cmp($D,&wparam(2));	# have we reached the end yet?
	&mov(&DWP(8,$tmp1),$A);
	 &mov($E,$C);		# C is last "E" which needs to be "pre-loaded"
	&mov(&DWP(12,$tmp1),$B);
	 &mov($T,$D);		# input pointer
	&mov(&DWP(16,$tmp1),$C);
	&jb(&label("loop"));

	&stack_pop(16+3);
&function_end("sha1_block_data_order");

if ($xmm) {
######################################################################
# The SSSE3 implementation.
#
# %xmm[0-7] are used as ring @X[] buffer containing quadruples of last
# 32 elements of the message schedule or Xupdate outputs. First 4
# quadruples are simply byte-swapped input, next 4 are calculated
# according to method originally suggested by Dean Gaudet (modulo
# being implemented in SSSE3). Once 8 quadruples or 32 elements are
# collected, it switches to routine proposed by Max Locktyukhin.
#
# Calculations inevitably require temporary reqisters, and there are
# no %xmm registers left to spare. For this reason part of the ring
# buffer, X[2..4] to be specific, is offloaded to 3 quadriples ring
# buffer on the stack. Keep in mind that X[2] is alias X[-6], X[3] -
# X[-5], and X[4] - X[-4]...
#
# Another notable optimization is aggressive stack frame compression
# aiming to minimize amount of 9-byte instructions...
#
# Yet another notable optimization is "jumping" $B variable. It means
# that there is no register permanently allocated for $B value. This
# allowed to eliminate one instruction from body_20_39...
#
my $Xi=4;			# 4xSIMD Xupdate round, start pre-seeded
my @X=map("xmm$_",(4..7,0..3));	# pre-seeded for $Xi=4
my @V=($A,$B,$C,$D,$E);
my $j=0;			# hash round
my @T=($T,$tmp1);
my $inp;

my $_rol=sub { &rol(@_) };
my $_ror=sub { &ror(@_) };

&function_begin("_sha1_block_data_order_ssse3");
	&picsetup($tmp1);
	&picsymbol($tmp1, &label("K_XX_XX"), $tmp1);

&set_label("ssse3_shortcut");

	&movdqa	(@X[3],&QWP(0,$tmp1));		# K_00_19
	&movdqa	(@X[4],&QWP(16,$tmp1));		# K_20_39
	&movdqa	(@X[5],&QWP(32,$tmp1));		# K_40_59
	&movdqa	(@X[6],&QWP(48,$tmp1));		# K_60_79
	&movdqa	(@X[2],&QWP(64,$tmp1));		# pbswap mask

	&mov	($E,&wparam(0));		# load argument block
	&mov	($inp=@T[1],&wparam(1));
	&mov	($D,&wparam(2));
	&mov	(@T[0],"esp");

	# stack frame layout
	#
	# +0	X[0]+K	X[1]+K	X[2]+K	X[3]+K	# XMM->IALU xfer area
	#	X[4]+K	X[5]+K	X[6]+K	X[7]+K
	#	X[8]+K	X[9]+K	X[10]+K	X[11]+K
	#	X[12]+K	X[13]+K	X[14]+K	X[15]+K
	#
	# +64	X[0]	X[1]	X[2]	X[3]	# XMM->XMM backtrace area
	#	X[4]	X[5]	X[6]	X[7]
	#	X[8]	X[9]	X[10]	X[11]	# even borrowed for K_00_19
	#
	# +112	K_20_39	K_20_39	K_20_39	K_20_39	# constants
	#	K_40_59	K_40_59	K_40_59	K_40_59
	#	K_60_79	K_60_79	K_60_79	K_60_79
	#	K_00_19	K_00_19	K_00_19	K_00_19
	#	pbswap mask
	#
	# +192	ctx				# argument block
	# +196	inp
	# +200	end
	# +204	esp
	&sub	("esp",208);
	&and	("esp",-64);

	&movdqa	(&QWP(112+0,"esp"),@X[4]);	# copy constants
	&movdqa	(&QWP(112+16,"esp"),@X[5]);
	&movdqa	(&QWP(112+32,"esp"),@X[6]);
	&shl	($D,6);				# len*64
	&movdqa	(&QWP(112+48,"esp"),@X[3]);
	&add	($D,$inp);			# end of input
	&movdqa	(&QWP(112+64,"esp"),@X[2]);
	&add	($inp,64);
	&mov	(&DWP(192+0,"esp"),$E);		# save argument block
	&mov	(&DWP(192+4,"esp"),$inp);
	&mov	(&DWP(192+8,"esp"),$D);
	&mov	(&DWP(192+12,"esp"),@T[0]);	# save original %esp

	&mov	($A,&DWP(0,$E));		# load context
	&mov	($B,&DWP(4,$E));
	&mov	($C,&DWP(8,$E));
	&mov	($D,&DWP(12,$E));
	&mov	($E,&DWP(16,$E));
	&mov	(@T[0],$B);			# magic seed

	&movdqu	(@X[-4&7],&QWP(-64,$inp));	# load input to %xmm[0-3]
	&movdqu	(@X[-3&7],&QWP(-48,$inp));
	&movdqu	(@X[-2&7],&QWP(-32,$inp));
	&movdqu	(@X[-1&7],&QWP(-16,$inp));
	&pshufb	(@X[-4&7],@X[2]);		# byte swap
	&pshufb	(@X[-3&7],@X[2]);
	&pshufb	(@X[-2&7],@X[2]);
	&movdqa	(&QWP(112-16,"esp"),@X[3]);	# borrow last backtrace slot
	&pshufb	(@X[-1&7],@X[2]);
	&paddd	(@X[-4&7],@X[3]);		# add K_00_19
	&paddd	(@X[-3&7],@X[3]);
	&paddd	(@X[-2&7],@X[3]);
	&movdqa	(&QWP(0,"esp"),@X[-4&7]);	# X[]+K xfer to IALU
	&psubd	(@X[-4&7],@X[3]);		# restore X[]
	&movdqa	(&QWP(0+16,"esp"),@X[-3&7]);
	&psubd	(@X[-3&7],@X[3]);
	&movdqa	(&QWP(0+32,"esp"),@X[-2&7]);
	&psubd	(@X[-2&7],@X[3]);
	&movdqa	(@X[0],@X[-3&7]);
	&jmp	(&label("loop"));

######################################################################
# SSE instruction sequence is first broken to groups of independent
# instructions, independent in respect to their inputs and shifter
# (not all architectures have more than one). Then IALU instructions
# are "knitted in" between the SSE groups. Distance is maintained for
# SSE latency of 2 in hope that it fits better upcoming AMD Bulldozer
# [which allegedly also implements SSSE3]...
#
# Temporary registers usage. X[2] is volatile at the entry and at the
# end is restored from backtrace ring buffer. X[3] is expected to
# contain current K_XX_XX constant and is used to calculate X[-1]+K
# from previous round, it becomes volatile the moment the value is
# saved to stack for transfer to IALU. X[4] becomes volatile whenever
# X[-4] is accumulated and offloaded to backtrace ring buffer, at the
# end it is loaded with next K_XX_XX [which becomes X[3] in next
# round]...
#
sub Xupdate_ssse3_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&palignr(@X[0],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	&movdqa	(@X[2],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &paddd	(@X[3],@X[-1&7]);
	  &movdqa	(&QWP(64+16*(($Xi-4)%3),"esp"),@X[-4&7]);# save X[] to backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psrldq	(@X[2],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));
	&pxor	(@X[0],@X[-4&7]);	# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[2],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@X[2]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &movdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&movdqa	(@X[4],@X[0]);
	&movdqa	(@X[2],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pslldq	(@X[4],12);		# "X[0]"<<96, extract one dword
	&paddd	(@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@X[2],31);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&movdqa	(@X[3],@X[4]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@X[4],30);
	&por	(@X[0],@X[2]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &movdqa	(@X[2],&QWP(64+16*(($Xi-6)%3),"esp")) if ($Xi>5);	# restore X[] from backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pslld	(@X[3],2);
	&pxor	(@X[0],@X[4]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &movdqa	(@X[4],&QWP(112-16+16*(($Xi)/5),"esp"));	# K_XX_XX
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@X[3]);		# "X[0]"^=("X[0]"<<96)<<<2
	  &movdqa	(@X[1],@X[-2&7])	if ($Xi<7);
	 eval(shift(@insns));
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_ssse3_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 48 instructions
  my ($a,$b,$c,$d,$e);

	&movdqa	(@X[2],@X[-1&7])	if ($Xi==8);
	 eval(shift(@insns));		# body_20_39
	&pxor	(@X[0],@X[-4&7]);	# "X[0]"="X[-32]"^"X[-16]"
	&palignr(@X[2],@X[-2&7],8);	# compose "X[-6]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&pxor	(@X[0],@X[-7&7]);	# "X[0]"^="X[-28]"
	  &movdqa	(&QWP(64+16*(($Xi-4)%3),"esp"),@X[-4&7]);	# save X[] to backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));
	 if ($Xi%5) {
	  &movdqa	(@X[4],@X[3]);	# "perpetuate" K_XX_XX...
	 } else {			# ... or load next one
	  &movdqa	(@X[4],&QWP(112-16+16*($Xi/5),"esp"));
	 }
	  &paddd	(@X[3],@X[-1&7]);
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&pxor	(@X[0],@X[2]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&movdqa	(@X[2],@X[0]);
	  &movdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&pslld	(@X[0],2);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	&psrld	(@X[2],30);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&por	(@X[0],@X[2]);		# "X[0]"<<<=2
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	  &movdqa	(@X[2],&QWP(64+16*(($Xi-6)%3),"esp")) if($Xi<19);	# restore X[] from backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	  &movdqa	(@X[3],@X[0])	if ($Xi<19);
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xuplast_ssse3_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	  &paddd	(@X[3],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &movdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&mov	($inp=@T[1],&DWP(192+4,"esp"));
	&cmp	($inp,&DWP(192+8,"esp"));
	&je	(&label("done"));

	&movdqa	(@X[3],&QWP(112+48,"esp"));	# K_00_19
	&movdqa	(@X[2],&QWP(112+64,"esp"));	# pbswap mask
	&movdqu	(@X[-4&7],&QWP(0,$inp));	# load input
	&movdqu	(@X[-3&7],&QWP(16,$inp));
	&movdqu	(@X[-2&7],&QWP(32,$inp));
	&movdqu	(@X[-1&7],&QWP(48,$inp));
	&add	($inp,64);
	&pshufb	(@X[-4&7],@X[2]);		# byte swap
	&mov	(&DWP(192+4,"esp"),$inp);
	&movdqa	(&QWP(112-16,"esp"),@X[3]);	# borrow last backtrace slot

  $Xi=0;
}

sub Xloop_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&pshufb	(@X[($Xi-3)&7],@X[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&paddd	(@X[($Xi-4)&7],@X[3]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&movdqa	(&QWP(0+16*$Xi,"esp"),@X[($Xi-4)&7]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psubd	(@X[($Xi-4)&7],@X[3]);

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

sub body_00_19 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,&DWP(4*($j&15),"esp"));',	# X[]+K xfer
	'&xor	($c,$d);',
	'&mov	(@T[1],$a);',	# $b in next round
	'&$_rol	($a,5);',
	'&and	(@T[0],$c);',	# ($b&($c^$d))
	'&xor	($c,$d);',	# restore $c
	'&xor	(@T[0],$d);',
	'&add	($e,$a);',
	'&$_ror	($b,$j?7:2);',	# $b>>>2
	'&add	($e,@T[0]);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}

sub body_20_39 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,&DWP(4*($j++&15),"esp"));',	# X[]+K xfer
	'&xor	(@T[0],$d);',	# ($b^$d)
	'&mov	(@T[1],$a);',	# $b in next round
	'&$_rol	($a,5);',
	'&xor	(@T[0],$c);',	# ($b^$d^$c)
	'&add	($e,$a);',
	'&$_ror	($b,7);',	# $b>>>2
	'&add	($e,@T[0]);'	.'unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}

sub body_40_59 () {
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&mov	(@T[1],$c);',
	'&xor	($c,$d);',
	'&add	($e,&DWP(4*($j++&15),"esp"));',	# X[]+K xfer
	'&and	(@T[1],$d);',
	'&and	(@T[0],$c);',	# ($b&($c^$d))
	'&$_ror	($b,7);',	# $b>>>2
	'&add	($e,@T[1]);',
	'&mov	(@T[1],$a);',	# $b in next round
	'&$_rol	($a,5);',
	'&add	($e,@T[0]);',
	'&xor	($c,$d);',	# restore $c
	'&add	($e,$a);'	.'unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}

&set_label("loop",16);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xuplast_ssse3_80(\&body_20_39);	# can jump to "done"

				$saved_j=$j; @saved_V=@V;

	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);

	&mov	(@T[1],&DWP(192,"esp"));	# update context
	&add	($A,&DWP(0,@T[1]));
	&add	(@T[0],&DWP(4,@T[1]));		# $b
	&add	($C,&DWP(8,@T[1]));
	&mov	(&DWP(0,@T[1]),$A);
	&add	($D,&DWP(12,@T[1]));
	&mov	(&DWP(4,@T[1]),@T[0]);
	&add	($E,&DWP(16,@T[1]));
	&mov	(&DWP(8,@T[1]),$C);
	&mov	($B,@T[0]);
	&mov	(&DWP(12,@T[1]),$D);
	&mov	(&DWP(16,@T[1]),$E);
	&movdqa	(@X[0],@X[-3&7]);

	&jmp	(&label("loop"));

&set_label("done",16);		$j=$saved_j; @V=@saved_V;

	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);

	&mov	(@T[1],&DWP(192,"esp"));	# update context
	&add	($A,&DWP(0,@T[1]));
	&mov	("esp",&DWP(192+12,"esp"));	# restore %esp
	&add	(@T[0],&DWP(4,@T[1]));		# $b
	&add	($C,&DWP(8,@T[1]));
	&mov	(&DWP(0,@T[1]),$A);
	&add	($D,&DWP(12,@T[1]));
	&mov	(&DWP(4,@T[1]),@T[0]);
	&add	($E,&DWP(16,@T[1]));
	&mov	(&DWP(8,@T[1]),$C);
	&mov	(&DWP(12,@T[1]),$D);
	&mov	(&DWP(16,@T[1]),$E);

&function_end("_sha1_block_data_order_ssse3");

if ($ymm) {
my $Xi=4;			# 4xSIMD Xupdate round, start pre-seeded
my @X=map("xmm$_",(4..7,0..3));	# pre-seeded for $Xi=4
my @V=($A,$B,$C,$D,$E);
my $j=0;			# hash round
my @T=($T,$tmp1);
my $inp;

my $_rol=sub { &shld(@_[0],@_) };
my $_ror=sub { &shrd(@_[0],@_) };

&function_begin("_sha1_block_data_order_avx");
	&picsetup($tmp1);
	&picsymbol($tmp1, &label("K_XX_XX"), $tmp1);

&set_label("avx_shortcut");
	&vzeroall();

	&vmovdqa(@X[3],&QWP(0,$tmp1));		# K_00_19
	&vmovdqa(@X[4],&QWP(16,$tmp1));		# K_20_39
	&vmovdqa(@X[5],&QWP(32,$tmp1));		# K_40_59
	&vmovdqa(@X[6],&QWP(48,$tmp1));		# K_60_79
	&vmovdqa(@X[2],&QWP(64,$tmp1));		# pbswap mask

	&mov	($E,&wparam(0));		# load argument block
	&mov	($inp=@T[1],&wparam(1));
	&mov	($D,&wparam(2));
	&mov	(@T[0],"esp");

	# stack frame layout
	#
	# +0	X[0]+K	X[1]+K	X[2]+K	X[3]+K	# XMM->IALU xfer area
	#	X[4]+K	X[5]+K	X[6]+K	X[7]+K
	#	X[8]+K	X[9]+K	X[10]+K	X[11]+K
	#	X[12]+K	X[13]+K	X[14]+K	X[15]+K
	#
	# +64	X[0]	X[1]	X[2]	X[3]	# XMM->XMM backtrace area
	#	X[4]	X[5]	X[6]	X[7]
	#	X[8]	X[9]	X[10]	X[11]	# even borrowed for K_00_19
	#
	# +112	K_20_39	K_20_39	K_20_39	K_20_39	# constants
	#	K_40_59	K_40_59	K_40_59	K_40_59
	#	K_60_79	K_60_79	K_60_79	K_60_79
	#	K_00_19	K_00_19	K_00_19	K_00_19
	#	pbswap mask
	#
	# +192	ctx				# argument block
	# +196	inp
	# +200	end
	# +204	esp
	&sub	("esp",208);
	&and	("esp",-64);

	&vmovdqa(&QWP(112+0,"esp"),@X[4]);	# copy constants
	&vmovdqa(&QWP(112+16,"esp"),@X[5]);
	&vmovdqa(&QWP(112+32,"esp"),@X[6]);
	&shl	($D,6);				# len*64
	&vmovdqa(&QWP(112+48,"esp"),@X[3]);
	&add	($D,$inp);			# end of input
	&vmovdqa(&QWP(112+64,"esp"),@X[2]);
	&add	($inp,64);
	&mov	(&DWP(192+0,"esp"),$E);		# save argument block
	&mov	(&DWP(192+4,"esp"),$inp);
	&mov	(&DWP(192+8,"esp"),$D);
	&mov	(&DWP(192+12,"esp"),@T[0]);	# save original %esp

	&mov	($A,&DWP(0,$E));		# load context
	&mov	($B,&DWP(4,$E));
	&mov	($C,&DWP(8,$E));
	&mov	($D,&DWP(12,$E));
	&mov	($E,&DWP(16,$E));
	&mov	(@T[0],$B);			# magic seed

	&vmovdqu(@X[-4&7],&QWP(-64,$inp));	# load input to %xmm[0-3]
	&vmovdqu(@X[-3&7],&QWP(-48,$inp));
	&vmovdqu(@X[-2&7],&QWP(-32,$inp));
	&vmovdqu(@X[-1&7],&QWP(-16,$inp));
	&vpshufb(@X[-4&7],@X[-4&7],@X[2]);	# byte swap
	&vpshufb(@X[-3&7],@X[-3&7],@X[2]);
	&vpshufb(@X[-2&7],@X[-2&7],@X[2]);
	&vmovdqa(&QWP(112-16,"esp"),@X[3]);	# borrow last backtrace slot
	&vpshufb(@X[-1&7],@X[-1&7],@X[2]);
	&vpaddd	(@X[0],@X[-4&7],@X[3]);		# add K_00_19
	&vpaddd	(@X[1],@X[-3&7],@X[3]);
	&vpaddd	(@X[2],@X[-2&7],@X[3]);
	&vmovdqa(&QWP(0,"esp"),@X[0]);		# X[]+K xfer to IALU
	&vmovdqa(&QWP(0+16,"esp"),@X[1]);
	&vmovdqa(&QWP(0+32,"esp"),@X[2]);
	&jmp	(&label("loop"));

sub Xupdate_avx_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpalignr(@X[0],@X[-3&7],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vpaddd	(@X[3],@X[3],@X[-1&7]);
	  &vmovdqa	(&QWP(64+16*(($Xi-4)%3),"esp"),@X[-4&7]);# save X[] to backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpsrldq(@X[2],@X[-1&7],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[2],@X[2],@X[-2&7]);		# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@X[2]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@X[2],@X[0],31);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslldq(@X[4],@X[0],12);		# "X[0]"<<96, extract one dword
	&vpaddd	(@X[0],@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@X[3],@X[4],30);
	&vpor	(@X[0],@X[0],@X[2]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslld	(@X[4],@X[4],2);
	  &vmovdqa	(@X[2],&QWP(64+16*(($Xi-6)%3),"esp")) if ($Xi>5);	# restore X[] from backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpxor	(@X[0],@X[0],@X[3]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@X[4]);		# "X[0]"^=("X[0]"<<96)<<<2
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	(@X[4],&QWP(112-16+16*(($Xi)/5),"esp"));	# K_XX_XX
	 eval(shift(@insns));
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_avx_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 48 instructions
  my ($a,$b,$c,$d,$e);

	&vpalignr(@X[2],@X[-1&7],@X[-2&7],8);	# compose "X[-6]"
	&vpxor	(@X[0],@X[0],@X[-4&7]);	# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpxor	(@X[0],@X[0],@X[-7&7]);	# "X[0]"^="X[-28]"
	  &vmovdqa	(&QWP(64+16*(($Xi-4)%3),"esp"),@X[-4&7]);	# save X[] to backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));
	 if ($Xi%5) {
	  &vmovdqa	(@X[4],@X[3]);	# "perpetuate" K_XX_XX...
	 } else {			# ... or load next one
	  &vmovdqa	(@X[4],&QWP(112-16+16*($Xi/5),"esp"));
	 }
	  &vpaddd	(@X[3],@X[3],@X[-1&7]);
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@X[2]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpsrld	(@X[2],@X[0],30);
	  &vmovdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpslld	(@X[0],@X[0],2);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpor	(@X[0],@X[0],@X[2]);	# "X[0]"<<<=2
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	  &vmovdqa	(@X[2],&QWP(64+16*(($Xi-6)%3),"esp")) if($Xi<19);	# restore X[] from backtrace buffer
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xuplast_avx_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	  &vpaddd	(@X[3],@X[3],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vmovdqa	(&QWP(0+16*(($Xi-1)&3),"esp"),@X[3]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&mov	($inp=@T[1],&DWP(192+4,"esp"));
	&cmp	($inp,&DWP(192+8,"esp"));
	&je	(&label("done"));

	&vmovdqa(@X[3],&QWP(112+48,"esp"));	# K_00_19
	&vmovdqa(@X[2],&QWP(112+64,"esp"));	# pbswap mask
	&vmovdqu(@X[-4&7],&QWP(0,$inp));	# load input
	&vmovdqu(@X[-3&7],&QWP(16,$inp));
	&vmovdqu(@X[-2&7],&QWP(32,$inp));
	&vmovdqu(@X[-1&7],&QWP(48,$inp));
	&add	($inp,64);
	&vpshufb(@X[-4&7],@X[-4&7],@X[2]);		# byte swap
	&mov	(&DWP(192+4,"esp"),$inp);
	&vmovdqa(&QWP(112-16,"esp"),@X[3]);	# borrow last backtrace slot

  $Xi=0;
}

sub Xloop_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpshufb	(@X[($Xi-3)&7],@X[($Xi-3)&7],@X[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpaddd	(@X[$Xi&7],@X[($Xi-4)&7],@X[3]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vmovdqa	(&QWP(0+16*$Xi,"esp"),@X[$Xi&7]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

&set_label("loop",16);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_32_79(\&body_00_19);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xuplast_avx_80(\&body_20_39);	# can jump to "done"

				$saved_j=$j; @saved_V=@V;

	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);

	&mov	(@T[1],&DWP(192,"esp"));	# update context
	&add	($A,&DWP(0,@T[1]));
	&add	(@T[0],&DWP(4,@T[1]));		# $b
	&add	($C,&DWP(8,@T[1]));
	&mov	(&DWP(0,@T[1]),$A);
	&add	($D,&DWP(12,@T[1]));
	&mov	(&DWP(4,@T[1]),@T[0]);
	&add	($E,&DWP(16,@T[1]));
	&mov	(&DWP(8,@T[1]),$C);
	&mov	($B,@T[0]);
	&mov	(&DWP(12,@T[1]),$D);
	&mov	(&DWP(16,@T[1]),$E);

	&jmp	(&label("loop"));

&set_label("done",16);		$j=$saved_j; @V=@saved_V;

	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);

	&vzeroall();

	&mov	(@T[1],&DWP(192,"esp"));	# update context
	&add	($A,&DWP(0,@T[1]));
	&mov	("esp",&DWP(192+12,"esp"));	# restore %esp
	&add	(@T[0],&DWP(4,@T[1]));		# $b
	&add	($C,&DWP(8,@T[1]));
	&mov	(&DWP(0,@T[1]),$A);
	&add	($D,&DWP(12,@T[1]));
	&mov	(&DWP(4,@T[1]),@T[0]);
	&add	($E,&DWP(16,@T[1]));
	&mov	(&DWP(8,@T[1]),$C);
	&mov	(&DWP(12,@T[1]),$D);
	&mov	(&DWP(16,@T[1]),$E);
&function_end("_sha1_block_data_order_avx");
}

	&rodataseg();
&set_label("K_XX_XX",64);
&data_word(0x5a827999,0x5a827999,0x5a827999,0x5a827999);	# K_00_19
&data_word(0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1);	# K_20_39
&data_word(0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc);	# K_40_59
&data_word(0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6);	# K_60_79
&data_word(0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f);	# pbswap mask
	&previous();
}

&asm_finish();
