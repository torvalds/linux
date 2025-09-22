#!/usr/bin/env perl
#
# ====================================================================
# Written by Andy Polyakov <appro@fy.chalmers.se> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# This module implements support for Intel AES-NI extension. In
# OpenSSL context it's used with Intel engine, but can also be used as
# drop-in replacement for crypto/aes/asm/aes-x86_64.pl [see below for
# details].
#
# Performance.
#
# Given aes(enc|dec) instructions' latency asymptotic performance for
# non-parallelizable modes such as CBC encrypt is 3.75 cycles per byte
# processed with 128-bit key. And given their throughput asymptotic
# performance for parallelizable modes is 1.25 cycles per byte. Being
# asymptotic limit it's not something you commonly achieve in reality,
# but how close does one get? Below are results collected for
# different modes and block sized. Pairs of numbers are for en-/
# decryption.
#
#	16-byte     64-byte     256-byte    1-KB        8-KB
# ECB	4.25/4.25   1.38/1.38   1.28/1.28   1.26/1.26	1.26/1.26
# CTR	5.42/5.42   1.92/1.92   1.44/1.44   1.28/1.28   1.26/1.26
# CBC	4.38/4.43   4.15/1.43   4.07/1.32   4.07/1.29   4.06/1.28
# CCM	5.66/9.42   4.42/5.41   4.16/4.40   4.09/4.15   4.06/4.07   
# OFB	5.42/5.42   4.64/4.64   4.44/4.44   4.39/4.39   4.38/4.38
# CFB	5.73/5.85   5.56/5.62   5.48/5.56   5.47/5.55   5.47/5.55
#
# ECB, CTR, CBC and CCM results are free from EVP overhead. This means
# that otherwise used 'openssl speed -evp aes-128-??? -engine aesni
# [-decrypt]' will exhibit 10-15% worse results for smaller blocks.
# The results were collected with specially crafted speed.c benchmark
# in order to compare them with results reported in "Intel Advanced
# Encryption Standard (AES) New Instruction Set" White Paper Revision
# 3.0 dated May 2010. All above results are consistently better. This
# module also provides better performance for block sizes smaller than
# 128 bytes in points *not* represented in the above table.
#
# Looking at the results for 8-KB buffer.
#
# CFB and OFB results are far from the limit, because implementation
# uses "generic" CRYPTO_[c|o]fb128_encrypt interfaces relying on
# single-block aesni_encrypt, which is not the most optimal way to go.
# CBC encrypt result is unexpectedly high and there is no documented
# explanation for it. Seemingly there is a small penalty for feeding
# the result back to AES unit the way it's done in CBC mode. There is
# nothing one can do and the result appears optimal. CCM result is
# identical to CBC, because CBC-MAC is essentially CBC encrypt without
# saving output. CCM CTR "stays invisible," because it's neatly
# interleaved with CBC-MAC. This provides ~30% improvement over
# "straghtforward" CCM implementation with CTR and CBC-MAC performed
# disjointly. Parallelizable modes practically achieve the theoretical
# limit.
#
# Looking at how results vary with buffer size.
#
# Curves are practically saturated at 1-KB buffer size. In most cases
# "256-byte" performance is >95%, and "64-byte" is ~90% of "8-KB" one.
# CTR curve doesn't follow this pattern and is "slowest" changing one
# with "256-byte" result being 87% of "8-KB." This is because overhead
# in CTR mode is most computationally intensive. Small-block CCM
# decrypt is slower than encrypt, because first CTR and last CBC-MAC
# iterations can't be interleaved.
#
# Results for 192- and 256-bit keys.
#
# EVP-free results were observed to scale perfectly with number of
# rounds for larger block sizes, i.e. 192-bit result being 10/12 times
# lower and 256-bit one - 10/14. Well, in CBC encrypt case differences
# are a tad smaller, because the above mentioned penalty biases all
# results by same constant value. In similar way function call
# overhead affects small-block performance, as well as OFB and CFB
# results. Differences are not large, most common coefficients are
# 10/11.7 and 10/13.4 (as opposite to 10/12.0 and 10/14.0), but one
# observe even 10/11.2 and 10/12.4 (CTR, OFB, CFB)...

# January 2011
#
# While Westmere processor features 6 cycles latency for aes[enc|dec]
# instructions, which can be scheduled every second cycle, Sandy
# Bridge spends 8 cycles per instruction, but it can schedule them
# every cycle. This means that code targeting Westmere would perform
# suboptimally on Sandy Bridge. Therefore this update.
#
# In addition, non-parallelizable CBC encrypt (as well as CCM) is
# optimized. Relative improvement might appear modest, 8% on Westmere,
# but in absolute terms it's 3.77 cycles per byte encrypted with
# 128-bit key on Westmere, and 5.07 - on Sandy Bridge. These numbers
# should be compared to asymptotic limits of 3.75 for Westmere and
# 5.00 for Sandy Bridge. Actually, the fact that they get this close
# to asymptotic limits is quite amazing. Indeed, the limit is
# calculated as latency times number of rounds, 10 for 128-bit key,
# and divided by 16, the number of bytes in block, or in other words
# it accounts *solely* for aesenc instructions. But there are extra
# instructions, and numbers so close to the asymptotic limits mean
# that it's as if it takes as little as *one* additional cycle to
# execute all of them. How is it possible? It is possible thanks to
# out-of-order execution logic, which manages to overlap post-
# processing of previous block, things like saving the output, with
# actual encryption of current block, as well as pre-processing of
# current block, things like fetching input and xor-ing it with
# 0-round element of the key schedule, with actual encryption of
# previous block. Keep this in mind...
#
# For parallelizable modes, such as ECB, CBC decrypt, CTR, higher
# performance is achieved by interleaving instructions working on
# independent blocks. In which case asymptotic limit for such modes
# can be obtained by dividing above mentioned numbers by AES
# instructions' interleave factor. Westmere can execute at most 3 
# instructions at a time, meaning that optimal interleave factor is 3,
# and that's where the "magic" number of 1.25 come from. "Optimal
# interleave factor" means that increase of interleave factor does
# not improve performance. The formula has proven to reflect reality
# pretty well on Westmere... Sandy Bridge on the other hand can
# execute up to 8 AES instructions at a time, so how does varying
# interleave factor affect the performance? Here is table for ECB
# (numbers are cycles per byte processed with 128-bit key):
#
# instruction interleave factor		3x	6x	8x
# theoretical asymptotic limit		1.67	0.83	0.625
# measured performance for 8KB block	1.05	0.86	0.84
#
# "as if" interleave factor		4.7x	5.8x	6.0x
#
# Further data for other parallelizable modes:
#
# CBC decrypt				1.16	0.93	0.93
# CTR					1.14	0.91	n/a
#
# Well, given 3x column it's probably inappropriate to call the limit
# asymptotic, if it can be surpassed, isn't it? What happens there?
# Rewind to CBC paragraph for the answer. Yes, out-of-order execution
# magic is responsible for this. Processor overlaps not only the
# additional instructions with AES ones, but even AES instructions
# processing adjacent triplets of independent blocks. In the 6x case
# additional instructions  still claim disproportionally small amount
# of additional cycles, but in 8x case number of instructions must be
# a tad too high for out-of-order logic to cope with, and AES unit
# remains underutilized... As you can see 8x interleave is hardly
# justifiable, so there no need to feel bad that 32-bit aesni-x86.pl
# utilizies 6x interleave because of limited register bank capacity.
#
# Higher interleave factors do have negative impact on Westmere
# performance. While for ECB mode it's negligible ~1.5%, other
# parallelizables perform ~5% worse, which is outweighed by ~25%
# improvement on Sandy Bridge. To balance regression on Westmere
# CTR mode was implemented with 6x aesenc interleave factor.

# April 2011
#
# Add aesni_xts_[en|de]crypt. Westmere spends 1.33 cycles processing
# one byte out of 8KB with 128-bit key, Sandy Bridge - 0.97. Just like
# in CTR mode AES instruction interleave factor was chosen to be 6x.

$PREFIX="aesni";	# if $PREFIX is set to "AES", the script
			# generates drop-in replacement for
			# crypto/aes/asm/aes-x86_64.pl:-)

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

$movkey = $PREFIX eq "aesni" ? "movups" : "movups";
@_4args=$win64?	("%rcx","%rdx","%r8", "%r9") :	# Win64 order
		("%rdi","%rsi","%rdx","%rcx");	# Unix order

$code=".text\n";

$rounds="%eax";	# input to and changed by aesni_[en|de]cryptN !!!
# this is natural Unix argument order for public $PREFIX_[ecb|cbc]_encrypt ...
$inp="%rdi";
$out="%rsi";
$len="%rdx";
$key="%rcx";	# input to and changed by aesni_[en|de]cryptN !!!
$ivp="%r8";	# cbc, ctr, ...

$rnds_="%r10d";	# backup copy for $rounds
$key_="%r11";	# backup copy for $key

# %xmm register layout
$rndkey0="%xmm0";	$rndkey1="%xmm1";
$inout0="%xmm2";	$inout1="%xmm3";
$inout2="%xmm4";	$inout3="%xmm5";
$inout4="%xmm6";	$inout5="%xmm7";
$inout6="%xmm8";	$inout7="%xmm9";

$in2="%xmm6";		$in1="%xmm7";	# used in CBC decrypt, CTR, ...
$in0="%xmm8";		$iv="%xmm9";

# Inline version of internal aesni_[en|de]crypt1.
#
# Why folded loop? Because aes[enc|dec] is slow enough to accommodate
# cycles which take care of loop variables...
{ my $sn;
sub aesni_generate1 {
my ($p,$key,$rounds,$inout,$ivec)=@_;	$inout=$inout0 if (!defined($inout));
++$sn;
$code.=<<___;
	$movkey	($key),$rndkey0
	$movkey	16($key),$rndkey1
___
$code.=<<___ if (defined($ivec));
	xorps	$rndkey0,$ivec
	lea	32($key),$key
	xorps	$ivec,$inout
___
$code.=<<___ if (!defined($ivec));
	lea	32($key),$key
	xorps	$rndkey0,$inout
___
$code.=<<___;
.Loop_${p}1_$sn:
	aes${p}	$rndkey1,$inout
	dec	$rounds
	$movkey	($key),$rndkey1
	lea	16($key),$key
	jnz	.Loop_${p}1_$sn	# loop body is 16 bytes
	aes${p}last	$rndkey1,$inout
___
}}
# void $PREFIX_[en|de]crypt (const void *inp,void *out,const AES_KEY *key);
#
{ my ($inp,$out,$key) = @_4args;

$code.=<<___;
.globl	${PREFIX}_encrypt
.type	${PREFIX}_encrypt,\@abi-omnipotent
.align	16
${PREFIX}_encrypt:
	_CET_ENDBR
	movups	($inp),$inout0		# load input
	mov	240($key),$rounds	# key->rounds
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)		# output
	ret
.size	${PREFIX}_encrypt,.-${PREFIX}_encrypt

.globl	${PREFIX}_decrypt
.type	${PREFIX}_decrypt,\@abi-omnipotent
.align	16
${PREFIX}_decrypt:
	_CET_ENDBR
	movups	($inp),$inout0		# load input
	mov	240($key),$rounds	# key->rounds
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)		# output
	ret
.size	${PREFIX}_decrypt, .-${PREFIX}_decrypt
___
}

# _aesni_[en|de]cryptN are private interfaces, N denotes interleave
# factor. Why 3x subroutine were originally used in loops? Even though
# aes[enc|dec] latency was originally 6, it could be scheduled only
# every *2nd* cycle. Thus 3x interleave was the one providing optimal
# utilization, i.e. when subroutine's throughput is virtually same as
# of non-interleaved subroutine [for number of input blocks up to 3].
# This is why it makes no sense to implement 2x subroutine.
# aes[enc|dec] latency in next processor generation is 8, but the
# instructions can be scheduled every cycle. Optimal interleave for
# new processor is therefore 8x...
sub aesni_generate3 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-2] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt3,\@abi-omnipotent
.align	16
_aesni_${dir}rypt3:
	_CET_ENDBR
	$movkey	($key),$rndkey0
	shr	\$1,$rounds
	$movkey	16($key),$rndkey1
	lea	32($key),$key
	xorps	$rndkey0,$inout0
	xorps	$rndkey0,$inout1
	xorps	$rndkey0,$inout2
	$movkey		($key),$rndkey0

.L${dir}_loop3:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	dec		$rounds
	aes${dir}	$rndkey1,$inout2
	$movkey		16($key),$rndkey1
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	lea		32($key),$key
	aes${dir}	$rndkey0,$inout2
	$movkey		($key),$rndkey0
	jnz		.L${dir}_loop3

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	ret
.size	_aesni_${dir}rypt3,.-_aesni_${dir}rypt3
___
}
# 4x interleave is implemented to improve small block performance,
# most notably [and naturally] 4 block by ~30%. One can argue that one
# should have implemented 5x as well, but improvement would be <20%,
# so it's not worth it...
sub aesni_generate4 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-3] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt4,\@abi-omnipotent
.align	16
_aesni_${dir}rypt4:
	_CET_ENDBR
	$movkey	($key),$rndkey0
	shr	\$1,$rounds
	$movkey	16($key),$rndkey1
	lea	32($key),$key
	xorps	$rndkey0,$inout0
	xorps	$rndkey0,$inout1
	xorps	$rndkey0,$inout2
	xorps	$rndkey0,$inout3
	$movkey	($key),$rndkey0

.L${dir}_loop4:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	dec		$rounds
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	$movkey		16($key),$rndkey1
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	lea		32($key),$key
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	$movkey		($key),$rndkey0
	jnz		.L${dir}_loop4

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	ret
.size	_aesni_${dir}rypt4,.-_aesni_${dir}rypt4
___
}
sub aesni_generate6 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-5] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt6,\@abi-omnipotent
.align	16
_aesni_${dir}rypt6:
	_CET_ENDBR
	$movkey		($key),$rndkey0
	shr		\$1,$rounds
	$movkey		16($key),$rndkey1
	lea		32($key),$key
	xorps		$rndkey0,$inout0
	pxor		$rndkey0,$inout1
	aes${dir}	$rndkey1,$inout0
	pxor		$rndkey0,$inout2
	aes${dir}	$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	aes${dir}	$rndkey1,$inout2
	pxor		$rndkey0,$inout4
	aes${dir}	$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	dec		$rounds
	aes${dir}	$rndkey1,$inout4
	$movkey		($key),$rndkey0
	aes${dir}	$rndkey1,$inout5
	jmp		.L${dir}_loop6_enter
.align	16
.L${dir}_loop6:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	dec		$rounds
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
.L${dir}_loop6_enter:				# happens to be 16-byte aligned
	$movkey		16($key),$rndkey1
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	lea		32($key),$key
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	aes${dir}	$rndkey0,$inout4
	aes${dir}	$rndkey0,$inout5
	$movkey		($key),$rndkey0
	jnz		.L${dir}_loop6

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	aes${dir}last	$rndkey0,$inout4
	aes${dir}last	$rndkey0,$inout5
	ret
.size	_aesni_${dir}rypt6,.-_aesni_${dir}rypt6
___
}
sub aesni_generate8 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-7] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt8,\@abi-omnipotent
.align	16
_aesni_${dir}rypt8:
	_CET_ENDBR
	$movkey		($key),$rndkey0
	shr		\$1,$rounds
	$movkey		16($key),$rndkey1
	lea		32($key),$key
	xorps		$rndkey0,$inout0
	xorps		$rndkey0,$inout1
	aes${dir}	$rndkey1,$inout0
	pxor		$rndkey0,$inout2
	aes${dir}	$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	aes${dir}	$rndkey1,$inout2
	pxor		$rndkey0,$inout4
	aes${dir}	$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	dec		$rounds
	aes${dir}	$rndkey1,$inout4
	pxor		$rndkey0,$inout6
	aes${dir}	$rndkey1,$inout5
	pxor		$rndkey0,$inout7
	$movkey		($key),$rndkey0
	aes${dir}	$rndkey1,$inout6
	aes${dir}	$rndkey1,$inout7
	$movkey		16($key),$rndkey1
	jmp		.L${dir}_loop8_enter
.align	16
.L${dir}_loop8:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	dec		$rounds
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}	$rndkey1,$inout6
	aes${dir}	$rndkey1,$inout7
	$movkey		16($key),$rndkey1
.L${dir}_loop8_enter:				# happens to be 16-byte aligned
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	lea		32($key),$key
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	aes${dir}	$rndkey0,$inout4
	aes${dir}	$rndkey0,$inout5
	aes${dir}	$rndkey0,$inout6
	aes${dir}	$rndkey0,$inout7
	$movkey		($key),$rndkey0
	jnz		.L${dir}_loop8

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}	$rndkey1,$inout6
	aes${dir}	$rndkey1,$inout7
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	aes${dir}last	$rndkey0,$inout4
	aes${dir}last	$rndkey0,$inout5
	aes${dir}last	$rndkey0,$inout6
	aes${dir}last	$rndkey0,$inout7
	ret
.size	_aesni_${dir}rypt8,.-_aesni_${dir}rypt8
___
}
&aesni_generate3("enc") if ($PREFIX eq "aesni");
&aesni_generate3("dec");
&aesni_generate4("enc") if ($PREFIX eq "aesni");
&aesni_generate4("dec");
&aesni_generate6("enc") if ($PREFIX eq "aesni");
&aesni_generate6("dec");
&aesni_generate8("enc") if ($PREFIX eq "aesni");
&aesni_generate8("dec");

if ($PREFIX eq "aesni") {
########################################################################
# void aesni_ecb_encrypt (const void *in, void *out,
#			  size_t length, const AES_KEY *key,
#			  int enc);
$code.=<<___;
.globl	aesni_ecb_encrypt
.type	aesni_ecb_encrypt,\@function,5
.align	16
aesni_ecb_encrypt:
	_CET_ENDBR
	and	\$-16,$len
	jz	.Lecb_ret

	mov	240($key),$rounds	# key->rounds
	$movkey	($key),$rndkey0
	mov	$key,$key_		# backup $key
	mov	$rounds,$rnds_		# backup $rounds
	test	%r8d,%r8d		# 5th argument
	jz	.Lecb_decrypt
#--------------------------- ECB ENCRYPT ------------------------------#
	cmp	\$0x80,$len
	jb	.Lecb_enc_tail

	movdqu	($inp),$inout0
	movdqu	0x10($inp),$inout1
	movdqu	0x20($inp),$inout2
	movdqu	0x30($inp),$inout3
	movdqu	0x40($inp),$inout4
	movdqu	0x50($inp),$inout5
	movdqu	0x60($inp),$inout6
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp
	sub	\$0x80,$len
	jmp	.Lecb_enc_loop8_enter
.align 16
.Lecb_enc_loop8:
	movups	$inout0,($out)
	mov	$key_,$key		# restore $key
	movdqu	($inp),$inout0
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout1,0x10($out)
	movdqu	0x10($inp),$inout1
	movups	$inout2,0x20($out)
	movdqu	0x20($inp),$inout2
	movups	$inout3,0x30($out)
	movdqu	0x30($inp),$inout3
	movups	$inout4,0x40($out)
	movdqu	0x40($inp),$inout4
	movups	$inout5,0x50($out)
	movdqu	0x50($inp),$inout5
	movups	$inout6,0x60($out)
	movdqu	0x60($inp),$inout6
	movups	$inout7,0x70($out)
	lea	0x80($out),$out
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp
.Lecb_enc_loop8_enter:

	call	_aesni_encrypt8

	sub	\$0x80,$len
	jnc	.Lecb_enc_loop8

	movups	$inout0,($out)
	mov	$key_,$key		# restore $key
	movups	$inout1,0x10($out)
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	movups	$inout7,0x70($out)
	lea	0x80($out),$out
	add	\$0x80,$len
	jz	.Lecb_ret

.Lecb_enc_tail:
	movups	($inp),$inout0
	cmp	\$0x20,$len
	jb	.Lecb_enc_one
	movups	0x10($inp),$inout1
	je	.Lecb_enc_two
	movups	0x20($inp),$inout2
	cmp	\$0x40,$len
	jb	.Lecb_enc_three
	movups	0x30($inp),$inout3
	je	.Lecb_enc_four
	movups	0x40($inp),$inout4
	cmp	\$0x60,$len
	jb	.Lecb_enc_five
	movups	0x50($inp),$inout5
	je	.Lecb_enc_six
	movdqu	0x60($inp),$inout6
	call	_aesni_encrypt8
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_one:
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_two:
	xorps	$inout2,$inout2
	call	_aesni_encrypt3
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_three:
	call	_aesni_encrypt3
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_four:
	call	_aesni_encrypt4
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_five:
	xorps	$inout5,$inout5
	call	_aesni_encrypt6
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_six:
	call	_aesni_encrypt6
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	jmp	.Lecb_ret
#--------------------------- ECB DECRYPT ------------------------------#
.align	16
.Lecb_decrypt:
	cmp	\$0x80,$len
	jb	.Lecb_dec_tail

	movdqu	($inp),$inout0
	movdqu	0x10($inp),$inout1
	movdqu	0x20($inp),$inout2
	movdqu	0x30($inp),$inout3
	movdqu	0x40($inp),$inout4
	movdqu	0x50($inp),$inout5
	movdqu	0x60($inp),$inout6
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp
	sub	\$0x80,$len
	jmp	.Lecb_dec_loop8_enter
.align 16
.Lecb_dec_loop8:
	movups	$inout0,($out)
	mov	$key_,$key		# restore $key
	movdqu	($inp),$inout0
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout1,0x10($out)
	movdqu	0x10($inp),$inout1
	movups	$inout2,0x20($out)
	movdqu	0x20($inp),$inout2
	movups	$inout3,0x30($out)
	movdqu	0x30($inp),$inout3
	movups	$inout4,0x40($out)
	movdqu	0x40($inp),$inout4
	movups	$inout5,0x50($out)
	movdqu	0x50($inp),$inout5
	movups	$inout6,0x60($out)
	movdqu	0x60($inp),$inout6
	movups	$inout7,0x70($out)
	lea	0x80($out),$out
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp
.Lecb_dec_loop8_enter:

	call	_aesni_decrypt8

	$movkey	($key_),$rndkey0
	sub	\$0x80,$len
	jnc	.Lecb_dec_loop8

	movups	$inout0,($out)
	mov	$key_,$key		# restore $key
	movups	$inout1,0x10($out)
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	movups	$inout7,0x70($out)
	lea	0x80($out),$out
	add	\$0x80,$len
	jz	.Lecb_ret

.Lecb_dec_tail:
	movups	($inp),$inout0
	cmp	\$0x20,$len
	jb	.Lecb_dec_one
	movups	0x10($inp),$inout1
	je	.Lecb_dec_two
	movups	0x20($inp),$inout2
	cmp	\$0x40,$len
	jb	.Lecb_dec_three
	movups	0x30($inp),$inout3
	je	.Lecb_dec_four
	movups	0x40($inp),$inout4
	cmp	\$0x60,$len
	jb	.Lecb_dec_five
	movups	0x50($inp),$inout5
	je	.Lecb_dec_six
	movups	0x60($inp),$inout6
	$movkey	($key),$rndkey0
	call	_aesni_decrypt8
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_one:
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_two:
	xorps	$inout2,$inout2
	call	_aesni_decrypt3
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_three:
	call	_aesni_decrypt3
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_four:
	call	_aesni_decrypt4
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_five:
	xorps	$inout5,$inout5
	call	_aesni_decrypt6
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	jmp	.Lecb_ret
.align	16
.Lecb_dec_six:
	call	_aesni_decrypt6
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)

.Lecb_ret:
	ret
.size	aesni_ecb_encrypt,.-aesni_ecb_encrypt
___

{
######################################################################
# void aesni_ccm64_[en|de]crypt_blocks (const void *in, void *out,
#                         size_t blocks, const AES_KEY *key,
#                         const char *ivec,char *cmac);
#
# Handles only complete blocks, operates on 64-bit counter and
# does not update *ivec! Nor does it finalize CMAC value
# (see engine/eng_aesni.c for details)
#
{
my $cmac="%r9";	# 6th argument

my $increment="%xmm6";
my $bswap_mask="%xmm7";

$code.=<<___;
.globl	aesni_ccm64_encrypt_blocks
.type	aesni_ccm64_encrypt_blocks,\@function,6
.align	16
aesni_ccm64_encrypt_blocks:
	_CET_ENDBR
___
$code.=<<___ if ($win64);
	lea	-0x58(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
.Lccm64_enc_body:
___
$code.=<<___;
	mov	240($key),$rounds		# key->rounds
	movdqu	($ivp),$iv
	movdqa	.Lincrement64(%rip),$increment
	movdqa	.Lbswap_mask(%rip),$bswap_mask

	shr	\$1,$rounds
	lea	0($key),$key_
	movdqu	($cmac),$inout1
	movdqa	$iv,$inout0
	mov	$rounds,$rnds_
	pshufb	$bswap_mask,$iv
	jmp	.Lccm64_enc_outer
.align	16
.Lccm64_enc_outer:
	$movkey	($key_),$rndkey0
	mov	$rnds_,$rounds
	movups	($inp),$in0			# load inp

	xorps	$rndkey0,$inout0		# counter
	$movkey	16($key_),$rndkey1
	xorps	$in0,$rndkey0
	lea	32($key_),$key
	xorps	$rndkey0,$inout1		# cmac^=inp
	$movkey	($key),$rndkey0

.Lccm64_enc2_loop:
	aesenc	$rndkey1,$inout0
	dec	$rounds
	aesenc	$rndkey1,$inout1
	$movkey	16($key),$rndkey1
	aesenc	$rndkey0,$inout0
	lea	32($key),$key
	aesenc	$rndkey0,$inout1
	$movkey	0($key),$rndkey0
	jnz	.Lccm64_enc2_loop
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	paddq	$increment,$iv
	aesenclast	$rndkey0,$inout0
	aesenclast	$rndkey0,$inout1

	dec	$len
	lea	16($inp),$inp
	xorps	$inout0,$in0			# inp ^= E(iv)
	movdqa	$iv,$inout0
	movups	$in0,($out)			# save output
	lea	16($out),$out
	pshufb	$bswap_mask,$inout0
	jnz	.Lccm64_enc_outer

	movups	$inout1,($cmac)
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	lea	0x58(%rsp),%rsp
.Lccm64_enc_ret:
___
$code.=<<___;
	ret
.size	aesni_ccm64_encrypt_blocks,.-aesni_ccm64_encrypt_blocks
___
######################################################################
$code.=<<___;
.globl	aesni_ccm64_decrypt_blocks
.type	aesni_ccm64_decrypt_blocks,\@function,6
.align	16
aesni_ccm64_decrypt_blocks:
	_CET_ENDBR
___
$code.=<<___ if ($win64);
	lea	-0x58(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
.Lccm64_dec_body:
___
$code.=<<___;
	mov	240($key),$rounds		# key->rounds
	movups	($ivp),$iv
	movdqu	($cmac),$inout1
	movdqa	.Lincrement64(%rip),$increment
	movdqa	.Lbswap_mask(%rip),$bswap_mask

	movaps	$iv,$inout0
	mov	$rounds,$rnds_
	mov	$key,$key_
	pshufb	$bswap_mask,$iv
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	movups	($inp),$in0			# load inp
	paddq	$increment,$iv
	lea	16($inp),$inp
	jmp	.Lccm64_dec_outer
.align	16
.Lccm64_dec_outer:
	xorps	$inout0,$in0			# inp ^= E(iv)
	movdqa	$iv,$inout0
	mov	$rnds_,$rounds
	movups	$in0,($out)			# save output
	lea	16($out),$out
	pshufb	$bswap_mask,$inout0

	sub	\$1,$len
	jz	.Lccm64_dec_break

	$movkey	($key_),$rndkey0
	shr	\$1,$rounds
	$movkey	16($key_),$rndkey1
	xorps	$rndkey0,$in0
	lea	32($key_),$key
	xorps	$rndkey0,$inout0
	xorps	$in0,$inout1			# cmac^=out
	$movkey	($key),$rndkey0

.Lccm64_dec2_loop:
	aesenc	$rndkey1,$inout0
	dec	$rounds
	aesenc	$rndkey1,$inout1
	$movkey	16($key),$rndkey1
	aesenc	$rndkey0,$inout0
	lea	32($key),$key
	aesenc	$rndkey0,$inout1
	$movkey	0($key),$rndkey0
	jnz	.Lccm64_dec2_loop
	movups	($inp),$in0			# load inp
	paddq	$increment,$iv
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	lea	16($inp),$inp
	aesenclast	$rndkey0,$inout0
	aesenclast	$rndkey0,$inout1
	jmp	.Lccm64_dec_outer

.align	16
.Lccm64_dec_break:
	#xorps	$in0,$inout1			# cmac^=out
___
	&aesni_generate1("enc",$key_,$rounds,$inout1,$in0);
$code.=<<___;
	movups	$inout1,($cmac)
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	lea	0x58(%rsp),%rsp
.Lccm64_dec_ret:
___
$code.=<<___;
	ret
.size	aesni_ccm64_decrypt_blocks,.-aesni_ccm64_decrypt_blocks
___
}
######################################################################
# void aesni_ctr32_encrypt_blocks (const void *in, void *out,
#                         size_t blocks, const AES_KEY *key,
#                         const char *ivec);
#
# Handles only complete blocks, operates on 32-bit counter and
# does not update *ivec! (see engine/eng_aesni.c for details)
#
{
my $frame_size = 0x20+($win64?160:0);
my ($in0,$in1,$in2,$in3)=map("%xmm$_",(8..11));
my ($iv0,$iv1,$ivec)=("%xmm12","%xmm13","%xmm14");
my $bswap_mask="%xmm15";

$code.=<<___;
.globl	aesni_ctr32_encrypt_blocks
.type	aesni_ctr32_encrypt_blocks,\@function,5
.align	16
aesni_ctr32_encrypt_blocks:
	_CET_ENDBR
	lea	(%rsp),%rax
	push	%rbp
	sub	\$$frame_size,%rsp
___
$code.=<<___ if ($win64);
	movaps	%xmm6,0x20(%rsp)
	movaps	%xmm7,0x30(%rsp)
	movaps	%xmm8,0x40(%rsp)
	movaps	%xmm9,0x50(%rsp)
	movaps	%xmm10,0x60(%rsp)
	movaps	%xmm11,0x70(%rsp)
	movaps	%xmm12,0x80(%rsp)
	movaps	%xmm13,0x90(%rsp)
	movaps	%xmm14,0xa0(%rsp)
	movaps	%xmm15,0xb0(%rsp)
.Lctr32_body:
___
$code.=<<___;
	lea	-8(%rax),%rbp
	cmp	\$1,$len
	je	.Lctr32_one_shortcut

	movdqu	($ivp),$ivec
	movdqa	.Lbswap_mask(%rip),$bswap_mask
	xor	$rounds,$rounds
	pextrd	\$3,$ivec,$rnds_		# pull 32-bit counter
	pinsrd	\$3,$rounds,$ivec		# wipe 32-bit counter

	mov	240($key),$rounds		# key->rounds
	bswap	$rnds_
	pxor	$iv0,$iv0			# vector of 3 32-bit counters
	pxor	$iv1,$iv1			# vector of 3 32-bit counters
	pinsrd	\$0,$rnds_,$iv0
	lea	3($rnds_),$key_
	pinsrd	\$0,$key_,$iv1
	inc	$rnds_
	pinsrd	\$1,$rnds_,$iv0
	inc	$key_
	pinsrd	\$1,$key_,$iv1
	inc	$rnds_
	pinsrd	\$2,$rnds_,$iv0
	inc	$key_
	pinsrd	\$2,$key_,$iv1
	movdqa	$iv0,0x00(%rsp)
	pshufb	$bswap_mask,$iv0
	movdqa	$iv1,0x10(%rsp)
	pshufb	$bswap_mask,$iv1

	pshufd	\$`3<<6`,$iv0,$inout0		# place counter to upper dword
	pshufd	\$`2<<6`,$iv0,$inout1
	pshufd	\$`1<<6`,$iv0,$inout2
	cmp	\$6,$len
	jb	.Lctr32_tail
	shr	\$1,$rounds
	mov	$key,$key_			# backup $key
	mov	$rounds,$rnds_			# backup $rounds
	sub	\$6,$len
	jmp	.Lctr32_loop6

.align	16
.Lctr32_loop6:
	pshufd	\$`3<<6`,$iv1,$inout3
	por	$ivec,$inout0			# merge counter-less ivec
	 $movkey	($key_),$rndkey0
	pshufd	\$`2<<6`,$iv1,$inout4
	por	$ivec,$inout1
	 $movkey	16($key_),$rndkey1
	pshufd	\$`1<<6`,$iv1,$inout5
	por	$ivec,$inout2
	por	$ivec,$inout3
	 xorps		$rndkey0,$inout0
	por	$ivec,$inout4
	por	$ivec,$inout5

	# inline _aesni_encrypt6 and interleave last rounds
	# with own code...

	pxor		$rndkey0,$inout1
	aesenc		$rndkey1,$inout0
	lea		32($key_),$key
	pxor		$rndkey0,$inout2
	aesenc		$rndkey1,$inout1
	 movdqa		.Lincrement32(%rip),$iv1
	pxor		$rndkey0,$inout3
	aesenc		$rndkey1,$inout2
	 movdqa		(%rsp),$iv0
	pxor		$rndkey0,$inout4
	aesenc		$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	dec		$rounds
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	jmp		.Lctr32_enc_loop6_enter
.align	16
.Lctr32_enc_loop6:
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	dec		$rounds
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
.Lctr32_enc_loop6_enter:
	$movkey		16($key),$rndkey1
	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	lea		32($key),$key
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	jnz		.Lctr32_enc_loop6

	aesenc		$rndkey1,$inout0
	 paddd		$iv1,$iv0		# increment counter vector
	aesenc		$rndkey1,$inout1
	 paddd		0x10(%rsp),$iv1
	aesenc		$rndkey1,$inout2
	 movdqa		$iv0,0x00(%rsp)		# save counter vector
	aesenc		$rndkey1,$inout3
	 movdqa		$iv1,0x10(%rsp)
	aesenc		$rndkey1,$inout4
	 pshufb		$bswap_mask,$iv0	# byte swap
	aesenc		$rndkey1,$inout5
	 pshufb		$bswap_mask,$iv1

	aesenclast	$rndkey0,$inout0
	 movups		($inp),$in0		# load input
	aesenclast	$rndkey0,$inout1
	 movups		0x10($inp),$in1
	aesenclast	$rndkey0,$inout2
	 movups		0x20($inp),$in2
	aesenclast	$rndkey0,$inout3
	 movups		0x30($inp),$in3
	aesenclast	$rndkey0,$inout4
	 movups		0x40($inp),$rndkey1
	aesenclast	$rndkey0,$inout5
	 movups		0x50($inp),$rndkey0
	 lea	0x60($inp),$inp

	xorps	$inout0,$in0			# xor
	 pshufd	\$`3<<6`,$iv0,$inout0
	xorps	$inout1,$in1
	 pshufd	\$`2<<6`,$iv0,$inout1
	movups	$in0,($out)			# store output
	xorps	$inout2,$in2
	 pshufd	\$`1<<6`,$iv0,$inout2
	movups	$in1,0x10($out)
	xorps	$inout3,$in3
	movups	$in2,0x20($out)
	xorps	$inout4,$rndkey1
	movups	$in3,0x30($out)
	xorps	$inout5,$rndkey0
	movups	$rndkey1,0x40($out)
	movups	$rndkey0,0x50($out)
	lea	0x60($out),$out
	mov	$rnds_,$rounds
	sub	\$6,$len
	jnc	.Lctr32_loop6

	add	\$6,$len
	jz	.Lctr32_done
	mov	$key_,$key			# restore $key
	lea	1($rounds,$rounds),$rounds	# restore original value

.Lctr32_tail:
	por	$ivec,$inout0
	movups	($inp),$in0
	cmp	\$2,$len
	jb	.Lctr32_one

	por	$ivec,$inout1
	movups	0x10($inp),$in1
	je	.Lctr32_two

	pshufd	\$`3<<6`,$iv1,$inout3
	por	$ivec,$inout2
	movups	0x20($inp),$in2
	cmp	\$4,$len
	jb	.Lctr32_three

	pshufd	\$`2<<6`,$iv1,$inout4
	por	$ivec,$inout3
	movups	0x30($inp),$in3
	je	.Lctr32_four

	por	$ivec,$inout4
	xorps	$inout5,$inout5

	call	_aesni_encrypt6

	movups	0x40($inp),$rndkey1
	xorps	$inout0,$in0
	xorps	$inout1,$in1
	movups	$in0,($out)
	xorps	$inout2,$in2
	movups	$in1,0x10($out)
	xorps	$inout3,$in3
	movups	$in2,0x20($out)
	xorps	$inout4,$rndkey1
	movups	$in3,0x30($out)
	movups	$rndkey1,0x40($out)
	jmp	.Lctr32_done

.align	16
.Lctr32_one_shortcut:
	movups	($ivp),$inout0
	movups	($inp),$in0
	mov	240($key),$rounds		# key->rounds
.Lctr32_one:
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	xorps	$inout0,$in0
	movups	$in0,($out)
	jmp	.Lctr32_done

.align	16
.Lctr32_two:
	xorps	$inout2,$inout2
	call	_aesni_encrypt3
	xorps	$inout0,$in0
	xorps	$inout1,$in1
	movups	$in0,($out)
	movups	$in1,0x10($out)
	jmp	.Lctr32_done

.align	16
.Lctr32_three:
	call	_aesni_encrypt3
	xorps	$inout0,$in0
	xorps	$inout1,$in1
	movups	$in0,($out)
	xorps	$inout2,$in2
	movups	$in1,0x10($out)
	movups	$in2,0x20($out)
	jmp	.Lctr32_done

.align	16
.Lctr32_four:
	call	_aesni_encrypt4
	xorps	$inout0,$in0
	xorps	$inout1,$in1
	movups	$in0,($out)
	xorps	$inout2,$in2
	movups	$in1,0x10($out)
	xorps	$inout3,$in3
	movups	$in2,0x20($out)
	movups	$in3,0x30($out)

.Lctr32_done:
___
$code.=<<___ if ($win64);
	movaps	0x20(%rsp),%xmm6
	movaps	0x30(%rsp),%xmm7
	movaps	0x40(%rsp),%xmm8
	movaps	0x50(%rsp),%xmm9
	movaps	0x60(%rsp),%xmm10
	movaps	0x70(%rsp),%xmm11
	movaps	0x80(%rsp),%xmm12
	movaps	0x90(%rsp),%xmm13
	movaps	0xa0(%rsp),%xmm14
	movaps	0xb0(%rsp),%xmm15
___
$code.=<<___;
	lea	(%rbp),%rsp
	pop	%rbp
.Lctr32_ret:
	ret
.size	aesni_ctr32_encrypt_blocks,.-aesni_ctr32_encrypt_blocks
___
}

######################################################################
# void aesni_xts_[en|de]crypt(const char *inp,char *out,size_t len,
#	const AES_KEY *key1, const AES_KEY *key2
#	const unsigned char iv[16]);
#
{
my @tweak=map("%xmm$_",(10..15));
my ($twmask,$twres,$twtmp)=("%xmm8","%xmm9",@tweak[4]);
my ($key2,$ivp,$len_)=("%r8","%r9","%r9");
my $frame_size = 0x60 + ($win64?160:0);

$code.=<<___;
.globl	aesni_xts_encrypt
.type	aesni_xts_encrypt,\@function,6
.align	16
aesni_xts_encrypt:
	_CET_ENDBR
	lea	(%rsp),%rax
	push	%rbp
	sub	\$$frame_size,%rsp
___
$code.=<<___ if ($win64);
	movaps	%xmm6,0x60(%rsp)
	movaps	%xmm7,0x70(%rsp)
	movaps	%xmm8,0x80(%rsp)
	movaps	%xmm9,0x90(%rsp)
	movaps	%xmm10,0xa0(%rsp)
	movaps	%xmm11,0xb0(%rsp)
	movaps	%xmm12,0xc0(%rsp)
	movaps	%xmm13,0xd0(%rsp)
	movaps	%xmm14,0xe0(%rsp)
	movaps	%xmm15,0xf0(%rsp)
.Lxts_enc_body:
___
$code.=<<___;
	lea	-8(%rax),%rbp
	movups	($ivp),@tweak[5]		# load clear-text tweak
	mov	240(%r8),$rounds		# key2->rounds
	mov	240($key),$rnds_		# key1->rounds
___
	# generate the tweak
	&aesni_generate1("enc",$key2,$rounds,@tweak[5]);
$code.=<<___;
	mov	$key,$key_			# backup $key
	mov	$rnds_,$rounds			# backup $rounds
	mov	$len,$len_			# backup $len
	and	\$-16,$len

	movdqa	.Lxts_magic(%rip),$twmask
	pxor	$twtmp,$twtmp
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
___
    for ($i=0;$i<4;$i++) {
    $code.=<<___;
	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[$i]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	pand	$twmask,$twres			# isolate carry and residue
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	pxor	$twres,@tweak[5]
___
    }
$code.=<<___;
	sub	\$16*6,$len
	jc	.Lxts_enc_short

	shr	\$1,$rounds
	sub	\$1,$rounds
	mov	$rounds,$rnds_
	jmp	.Lxts_enc_grandloop

.align	16
.Lxts_enc_grandloop:
	pshufd	\$0x13,$twtmp,$twres
	movdqa	@tweak[5],@tweak[4]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	movdqu	`16*0`($inp),$inout0		# load input
	pand	$twmask,$twres			# isolate carry and residue
	movdqu	`16*1`($inp),$inout1
	pxor	$twres,@tweak[5]

	movdqu	`16*2`($inp),$inout2
	pxor	@tweak[0],$inout0		# input^=tweak
	movdqu	`16*3`($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	`16*4`($inp),$inout4
	pxor	@tweak[2],$inout2
	movdqu	`16*5`($inp),$inout5
	lea	`16*6`($inp),$inp
	pxor	@tweak[3],$inout3
	$movkey		($key_),$rndkey0
	pxor	@tweak[4],$inout4
	pxor	@tweak[5],$inout5

	# inline _aesni_encrypt6 and interleave first and last rounds
	# with own code...
	$movkey		16($key_),$rndkey1
	pxor		$rndkey0,$inout0
	pxor		$rndkey0,$inout1
	 movdqa	@tweak[0],`16*0`(%rsp)		# put aside tweaks
	aesenc		$rndkey1,$inout0
	lea		32($key_),$key
	pxor		$rndkey0,$inout2
	 movdqa	@tweak[1],`16*1`(%rsp)
	aesenc		$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	 movdqa	@tweak[2],`16*2`(%rsp)
	aesenc		$rndkey1,$inout2
	pxor		$rndkey0,$inout4
	 movdqa	@tweak[3],`16*3`(%rsp)
	aesenc		$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	dec		$rounds
	 movdqa	@tweak[4],`16*4`(%rsp)
	aesenc		$rndkey1,$inout4
	 movdqa	@tweak[5],`16*5`(%rsp)
	aesenc		$rndkey1,$inout5
	pxor	$twtmp,$twtmp
	pcmpgtd	@tweak[5],$twtmp
	jmp		.Lxts_enc_loop6_enter

.align	16
.Lxts_enc_loop6:
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	dec		$rounds
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
.Lxts_enc_loop6_enter:
	$movkey		16($key),$rndkey1
	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	lea		32($key),$key
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	jnz		.Lxts_enc_loop6

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesenc		$rndkey1,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesenc		$rndkey1,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesenc		$rndkey1,$inout2
	pxor	$twres,@tweak[5]
	 aesenc		$rndkey1,$inout3
	 aesenc		$rndkey1,$inout4
	 aesenc		$rndkey1,$inout5
	 $movkey	16($key),$rndkey1

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[0]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesenc		$rndkey0,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesenc		$rndkey0,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesenc		$rndkey0,$inout2
	pxor	$twres,@tweak[5]
	 aesenc		$rndkey0,$inout3
	 aesenc		$rndkey0,$inout4
	 aesenc		$rndkey0,$inout5
	 $movkey	32($key),$rndkey0

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[1]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesenc		$rndkey1,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesenc		$rndkey1,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesenc		$rndkey1,$inout2
	pxor	$twres,@tweak[5]
	 aesenc		$rndkey1,$inout3
	 aesenc		$rndkey1,$inout4
	 aesenc		$rndkey1,$inout5

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[2]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesenclast	$rndkey0,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesenclast	$rndkey0,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesenclast	$rndkey0,$inout2
	pxor	$twres,@tweak[5]
	 aesenclast	$rndkey0,$inout3
	 aesenclast	$rndkey0,$inout4
	 aesenclast	$rndkey0,$inout5

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[3]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 xorps	`16*0`(%rsp),$inout0		# output^=tweak
	pand	$twmask,$twres			# isolate carry and residue
	 xorps	`16*1`(%rsp),$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	pxor	$twres,@tweak[5]

	xorps	`16*2`(%rsp),$inout2
	movups	$inout0,`16*0`($out)		# write output
	xorps	`16*3`(%rsp),$inout3
	movups	$inout1,`16*1`($out)
	xorps	`16*4`(%rsp),$inout4
	movups	$inout2,`16*2`($out)
	xorps	`16*5`(%rsp),$inout5
	movups	$inout3,`16*3`($out)
	mov	$rnds_,$rounds			# restore $rounds
	movups	$inout4,`16*4`($out)
	movups	$inout5,`16*5`($out)
	lea	`16*6`($out),$out
	sub	\$16*6,$len
	jnc	.Lxts_enc_grandloop

	lea	3($rounds,$rounds),$rounds	# restore original value
	mov	$key_,$key			# restore $key
	mov	$rounds,$rnds_			# backup $rounds

.Lxts_enc_short:
	add	\$16*6,$len
	jz	.Lxts_enc_done

	cmp	\$0x20,$len
	jb	.Lxts_enc_one
	je	.Lxts_enc_two

	cmp	\$0x40,$len
	jb	.Lxts_enc_three
	je	.Lxts_enc_four

	pshufd	\$0x13,$twtmp,$twres
	movdqa	@tweak[5],@tweak[4]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	 movdqu	($inp),$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 movdqu	16*1($inp),$inout1
	pxor	$twres,@tweak[5]

	movdqu	16*2($inp),$inout2
	pxor	@tweak[0],$inout0
	movdqu	16*3($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	16*4($inp),$inout4
	lea	16*5($inp),$inp
	pxor	@tweak[2],$inout2
	pxor	@tweak[3],$inout3
	pxor	@tweak[4],$inout4

	call	_aesni_encrypt6

	xorps	@tweak[0],$inout0
	movdqa	@tweak[5],@tweak[0]
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movdqu	$inout0,($out)
	xorps	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	xorps	@tweak[4],$inout4
	movdqu	$inout2,16*2($out)
	movdqu	$inout3,16*3($out)
	movdqu	$inout4,16*4($out)
	lea	16*5($out),$out
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_one:
	movups	($inp),$inout0
	lea	16*1($inp),$inp
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movdqa	@tweak[1],@tweak[0]
	movups	$inout0,($out)
	lea	16*1($out),$out
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_two:
	movups	($inp),$inout0
	movups	16($inp),$inout1
	lea	32($inp),$inp
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1

	call	_aesni_encrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[2],@tweak[0]
	xorps	@tweak[1],$inout1
	movups	$inout0,($out)
	movups	$inout1,16*1($out)
	lea	16*2($out),$out
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_three:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	lea	16*3($inp),$inp
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2

	call	_aesni_encrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[3],@tweak[0]
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	lea	16*3($out),$out
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_four:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	xorps	@tweak[0],$inout0
	movups	16*3($inp),$inout3
	lea	16*4($inp),$inp
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	xorps	@tweak[3],$inout3

	call	_aesni_encrypt4

	xorps	@tweak[0],$inout0
	movdqa	@tweak[5],@tweak[0]
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)
	xorps	@tweak[3],$inout3
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	movups	$inout3,16*3($out)
	lea	16*4($out),$out
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_done:
	and	\$15,$len_
	jz	.Lxts_enc_ret
	mov	$len_,$len

.Lxts_enc_steal:
	movzb	($inp),%eax			# borrow $rounds ...
	movzb	-16($out),%ecx			# ... and $key
	lea	1($inp),$inp
	mov	%al,-16($out)
	mov	%cl,0($out)
	lea	1($out),$out
	sub	\$1,$len
	jnz	.Lxts_enc_steal

	sub	$len_,$out			# rewind $out
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	-16($out),$inout0
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movups	$inout0,-16($out)

.Lxts_enc_ret:
___
$code.=<<___ if ($win64);
	movaps	0x60(%rsp),%xmm6
	movaps	0x70(%rsp),%xmm7
	movaps	0x80(%rsp),%xmm8
	movaps	0x90(%rsp),%xmm9
	movaps	0xa0(%rsp),%xmm10
	movaps	0xb0(%rsp),%xmm11
	movaps	0xc0(%rsp),%xmm12
	movaps	0xd0(%rsp),%xmm13
	movaps	0xe0(%rsp),%xmm14
	movaps	0xf0(%rsp),%xmm15
___
$code.=<<___;
	lea	(%rbp),%rsp
	pop	%rbp
.Lxts_enc_epilogue:
	ret
.size	aesni_xts_encrypt,.-aesni_xts_encrypt
___

$code.=<<___;
.globl	aesni_xts_decrypt
.type	aesni_xts_decrypt,\@function,6
.align	16
aesni_xts_decrypt:
	_CET_ENDBR
	lea	(%rsp),%rax
	push	%rbp
	sub	\$$frame_size,%rsp
___
$code.=<<___ if ($win64);
	movaps	%xmm6,0x60(%rsp)
	movaps	%xmm7,0x70(%rsp)
	movaps	%xmm8,0x80(%rsp)
	movaps	%xmm9,0x90(%rsp)
	movaps	%xmm10,0xa0(%rsp)
	movaps	%xmm11,0xb0(%rsp)
	movaps	%xmm12,0xc0(%rsp)
	movaps	%xmm13,0xd0(%rsp)
	movaps	%xmm14,0xe0(%rsp)
	movaps	%xmm15,0xf0(%rsp)
.Lxts_dec_body:
___
$code.=<<___;
	lea	-8(%rax),%rbp
	movups	($ivp),@tweak[5]		# load clear-text tweak
	mov	240($key2),$rounds		# key2->rounds
	mov	240($key),$rnds_		# key1->rounds
___
	# generate the tweak
	&aesni_generate1("enc",$key2,$rounds,@tweak[5]);
$code.=<<___;
	xor	%eax,%eax			# if ($len%16) len-=16;
	test	\$15,$len
	setnz	%al
	shl	\$4,%rax
	sub	%rax,$len

	mov	$key,$key_			# backup $key
	mov	$rnds_,$rounds			# backup $rounds
	mov	$len,$len_			# backup $len
	and	\$-16,$len

	movdqa	.Lxts_magic(%rip),$twmask
	pxor	$twtmp,$twtmp
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
___
    for ($i=0;$i<4;$i++) {
    $code.=<<___;
	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[$i]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	pand	$twmask,$twres			# isolate carry and residue
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	pxor	$twres,@tweak[5]
___
    }
$code.=<<___;
	sub	\$16*6,$len
	jc	.Lxts_dec_short

	shr	\$1,$rounds
	sub	\$1,$rounds
	mov	$rounds,$rnds_
	jmp	.Lxts_dec_grandloop

.align	16
.Lxts_dec_grandloop:
	pshufd	\$0x13,$twtmp,$twres
	movdqa	@tweak[5],@tweak[4]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	movdqu	`16*0`($inp),$inout0		# load input
	pand	$twmask,$twres			# isolate carry and residue
	movdqu	`16*1`($inp),$inout1
	pxor	$twres,@tweak[5]

	movdqu	`16*2`($inp),$inout2
	pxor	@tweak[0],$inout0		# input^=tweak
	movdqu	`16*3`($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	`16*4`($inp),$inout4
	pxor	@tweak[2],$inout2
	movdqu	`16*5`($inp),$inout5
	lea	`16*6`($inp),$inp
	pxor	@tweak[3],$inout3
	$movkey		($key_),$rndkey0
	pxor	@tweak[4],$inout4
	pxor	@tweak[5],$inout5

	# inline _aesni_decrypt6 and interleave first and last rounds
	# with own code...
	$movkey		16($key_),$rndkey1
	pxor		$rndkey0,$inout0
	pxor		$rndkey0,$inout1
	 movdqa	@tweak[0],`16*0`(%rsp)		# put aside tweaks
	aesdec		$rndkey1,$inout0
	lea		32($key_),$key
	pxor		$rndkey0,$inout2
	 movdqa	@tweak[1],`16*1`(%rsp)
	aesdec		$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	 movdqa	@tweak[2],`16*2`(%rsp)
	aesdec		$rndkey1,$inout2
	pxor		$rndkey0,$inout4
	 movdqa	@tweak[3],`16*3`(%rsp)
	aesdec		$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	dec		$rounds
	 movdqa	@tweak[4],`16*4`(%rsp)
	aesdec		$rndkey1,$inout4
	 movdqa	@tweak[5],`16*5`(%rsp)
	aesdec		$rndkey1,$inout5
	pxor	$twtmp,$twtmp
	pcmpgtd	@tweak[5],$twtmp
	jmp		.Lxts_dec_loop6_enter

.align	16
.Lxts_dec_loop6:
	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	dec		$rounds
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
.Lxts_dec_loop6_enter:
	$movkey		16($key),$rndkey1
	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	lea		32($key),$key
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	aesdec		$rndkey0,$inout4
	aesdec		$rndkey0,$inout5
	$movkey		($key),$rndkey0
	jnz		.Lxts_dec_loop6

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesdec		$rndkey1,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesdec		$rndkey1,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesdec		$rndkey1,$inout2
	pxor	$twres,@tweak[5]
	 aesdec		$rndkey1,$inout3
	 aesdec		$rndkey1,$inout4
	 aesdec		$rndkey1,$inout5
	 $movkey	16($key),$rndkey1

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[0]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesdec		$rndkey0,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesdec		$rndkey0,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesdec		$rndkey0,$inout2
	pxor	$twres,@tweak[5]
	 aesdec		$rndkey0,$inout3
	 aesdec		$rndkey0,$inout4
	 aesdec		$rndkey0,$inout5
	 $movkey	32($key),$rndkey0

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[1]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesdec		$rndkey1,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesdec		$rndkey1,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesdec		$rndkey1,$inout2
	pxor	$twres,@tweak[5]
	 aesdec		$rndkey1,$inout3
	 aesdec		$rndkey1,$inout4
	 aesdec		$rndkey1,$inout5

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[2]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 aesdeclast	$rndkey0,$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 aesdeclast	$rndkey0,$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	 aesdeclast	$rndkey0,$inout2
	pxor	$twres,@tweak[5]
	 aesdeclast	$rndkey0,$inout3
	 aesdeclast	$rndkey0,$inout4
	 aesdeclast	$rndkey0,$inout5

	pshufd	\$0x13,$twtmp,$twres
	pxor	$twtmp,$twtmp
	movdqa	@tweak[5],@tweak[3]
	paddq	@tweak[5],@tweak[5]		# psllq	1,$tweak
	 xorps	`16*0`(%rsp),$inout0		# output^=tweak
	pand	$twmask,$twres			# isolate carry and residue
	 xorps	`16*1`(%rsp),$inout1
	pcmpgtd	@tweak[5],$twtmp		# broadcast upper bits
	pxor	$twres,@tweak[5]

	xorps	`16*2`(%rsp),$inout2
	movups	$inout0,`16*0`($out)		# write output
	xorps	`16*3`(%rsp),$inout3
	movups	$inout1,`16*1`($out)
	xorps	`16*4`(%rsp),$inout4
	movups	$inout2,`16*2`($out)
	xorps	`16*5`(%rsp),$inout5
	movups	$inout3,`16*3`($out)
	mov	$rnds_,$rounds			# restore $rounds
	movups	$inout4,`16*4`($out)
	movups	$inout5,`16*5`($out)
	lea	`16*6`($out),$out
	sub	\$16*6,$len
	jnc	.Lxts_dec_grandloop

	lea	3($rounds,$rounds),$rounds	# restore original value
	mov	$key_,$key			# restore $key
	mov	$rounds,$rnds_			# backup $rounds

.Lxts_dec_short:
	add	\$16*6,$len
	jz	.Lxts_dec_done

	cmp	\$0x20,$len
	jb	.Lxts_dec_one
	je	.Lxts_dec_two

	cmp	\$0x40,$len
	jb	.Lxts_dec_three
	je	.Lxts_dec_four

	pshufd	\$0x13,$twtmp,$twres
	movdqa	@tweak[5],@tweak[4]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	 movdqu	($inp),$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 movdqu	16*1($inp),$inout1
	pxor	$twres,@tweak[5]

	movdqu	16*2($inp),$inout2
	pxor	@tweak[0],$inout0
	movdqu	16*3($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	16*4($inp),$inout4
	lea	16*5($inp),$inp
	pxor	@tweak[2],$inout2
	pxor	@tweak[3],$inout3
	pxor	@tweak[4],$inout4

	call	_aesni_decrypt6

	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movdqu	$inout0,($out)
	xorps	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	xorps	@tweak[4],$inout4
	movdqu	$inout2,16*2($out)
	 pxor		$twtmp,$twtmp
	movdqu	$inout3,16*3($out)
	 pcmpgtd	@tweak[5],$twtmp
	movdqu	$inout4,16*4($out)
	lea	16*5($out),$out
	 pshufd		\$0x13,$twtmp,@tweak[1]	# $twres
	and	\$15,$len_
	jz	.Lxts_dec_ret

	movdqa	@tweak[5],@tweak[0]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	pand	$twmask,@tweak[1]		# isolate carry and residue
	pxor	@tweak[5],@tweak[1]
	jmp	.Lxts_dec_done2

.align	16
.Lxts_dec_one:
	movups	($inp),$inout0
	lea	16*1($inp),$inp
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movdqa	@tweak[1],@tweak[0]
	movups	$inout0,($out)
	movdqa	@tweak[2],@tweak[1]
	lea	16*1($out),$out
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_two:
	movups	($inp),$inout0
	movups	16($inp),$inout1
	lea	32($inp),$inp
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1

	call	_aesni_decrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[2],@tweak[0]
	xorps	@tweak[1],$inout1
	movdqa	@tweak[3],@tweak[1]
	movups	$inout0,($out)
	movups	$inout1,16*1($out)
	lea	16*2($out),$out
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_three:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	lea	16*3($inp),$inp
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2

	call	_aesni_decrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[3],@tweak[0]
	xorps	@tweak[1],$inout1
	movdqa	@tweak[5],@tweak[1]
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	lea	16*3($out),$out
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_four:
	pshufd	\$0x13,$twtmp,$twres
	movdqa	@tweak[5],@tweak[4]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	 movups	($inp),$inout0
	pand	$twmask,$twres			# isolate carry and residue
	 movups	16*1($inp),$inout1
	pxor	$twres,@tweak[5]

	movups	16*2($inp),$inout2
	xorps	@tweak[0],$inout0
	movups	16*3($inp),$inout3
	lea	16*4($inp),$inp
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	xorps	@tweak[3],$inout3

	call	_aesni_decrypt4

	xorps	@tweak[0],$inout0
	movdqa	@tweak[4],@tweak[0]
	xorps	@tweak[1],$inout1
	movdqa	@tweak[5],@tweak[1]
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)
	xorps	@tweak[3],$inout3
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	movups	$inout3,16*3($out)
	lea	16*4($out),$out
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_done:
	and	\$15,$len_
	jz	.Lxts_dec_ret
.Lxts_dec_done2:
	mov	$len_,$len
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	($inp),$inout0
	xorps	@tweak[1],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[1],$inout0
	movups	$inout0,($out)

.Lxts_dec_steal:
	movzb	16($inp),%eax			# borrow $rounds ...
	movzb	($out),%ecx			# ... and $key
	lea	1($inp),$inp
	mov	%al,($out)
	mov	%cl,16($out)
	lea	1($out),$out
	sub	\$1,$len
	jnz	.Lxts_dec_steal

	sub	$len_,$out			# rewind $out
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	($out),$inout0
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movups	$inout0,($out)

.Lxts_dec_ret:
___
$code.=<<___ if ($win64);
	movaps	0x60(%rsp),%xmm6
	movaps	0x70(%rsp),%xmm7
	movaps	0x80(%rsp),%xmm8
	movaps	0x90(%rsp),%xmm9
	movaps	0xa0(%rsp),%xmm10
	movaps	0xb0(%rsp),%xmm11
	movaps	0xc0(%rsp),%xmm12
	movaps	0xd0(%rsp),%xmm13
	movaps	0xe0(%rsp),%xmm14
	movaps	0xf0(%rsp),%xmm15
___
$code.=<<___;
	lea	(%rbp),%rsp
	pop	%rbp
.Lxts_dec_epilogue:
	ret
.size	aesni_xts_decrypt,.-aesni_xts_decrypt
___
} }}

########################################################################
# void $PREFIX_cbc_encrypt (const void *inp, void *out,
#			    size_t length, const AES_KEY *key,
#			    unsigned char *ivp,const int enc);
{
my $frame_size = 0x10 + ($win64?0x40:0);	# used in decrypt
$code.=<<___;
.globl	${PREFIX}_cbc_encrypt
.type	${PREFIX}_cbc_encrypt,\@function,6
.align	16
${PREFIX}_cbc_encrypt:
	_CET_ENDBR
	test	$len,$len		# check length
	jz	.Lcbc_ret

	mov	240($key),$rnds_	# key->rounds
	mov	$key,$key_		# backup $key
	test	%r9d,%r9d		# 6th argument
	jz	.Lcbc_decrypt
#--------------------------- CBC ENCRYPT ------------------------------#
	movups	($ivp),$inout0		# load iv as initial state
	mov	$rnds_,$rounds
	cmp	\$16,$len
	jb	.Lcbc_enc_tail
	sub	\$16,$len
	jmp	.Lcbc_enc_loop
.align	16
.Lcbc_enc_loop:
	movups	($inp),$inout1		# load input
	lea	16($inp),$inp
	#xorps	$inout1,$inout0
___
	&aesni_generate1("enc",$key,$rounds,$inout0,$inout1);
$code.=<<___;
	mov	$rnds_,$rounds		# restore $rounds
	mov	$key_,$key		# restore $key
	movups	$inout0,0($out)		# store output
	lea	16($out),$out
	sub	\$16,$len
	jnc	.Lcbc_enc_loop
	add	\$16,$len
	jnz	.Lcbc_enc_tail
	movups	$inout0,($ivp)
	jmp	.Lcbc_ret

.Lcbc_enc_tail:
	mov	$len,%rcx	# zaps $key
	xchg	$inp,$out	# $inp is %rsi and $out is %rdi now
	.long	0x9066A4F3	# rep movsb
	mov	\$16,%ecx	# zero tail
	sub	$len,%rcx
	xor	%eax,%eax
	.long	0x9066AAF3	# rep stosb
	lea	-16(%rdi),%rdi	# rewind $out by 1 block
	mov	$rnds_,$rounds	# restore $rounds
	mov	%rdi,%rsi	# $inp and $out are the same
	mov	$key_,$key	# restore $key
	xor	$len,$len	# len=16
	jmp	.Lcbc_enc_loop	# one more spin
#--------------------------- CBC DECRYPT ------------------------------#
.align	16
.Lcbc_decrypt:
	lea	(%rsp),%rax
	push	%rbp
	sub	\$$frame_size,%rsp
___
$code.=<<___ if ($win64);
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
.Lcbc_decrypt_body:
___
$code.=<<___;
	lea	-8(%rax),%rbp
	movups	($ivp),$iv
	mov	$rnds_,$rounds
	cmp	\$0x70,$len
	jbe	.Lcbc_dec_tail
	shr	\$1,$rnds_
	sub	\$0x70,$len
	mov	$rnds_,$rounds
	movaps	$iv,(%rsp)
	jmp	.Lcbc_dec_loop8_enter
.align	16
.Lcbc_dec_loop8:
	movaps	$rndkey0,(%rsp)			# save IV
	movups	$inout7,($out)
	lea	0x10($out),$out
.Lcbc_dec_loop8_enter:
	$movkey		($key),$rndkey0
	movups	($inp),$inout0			# load input
	movups	0x10($inp),$inout1
	$movkey		16($key),$rndkey1

	lea		32($key),$key
	movdqu	0x20($inp),$inout2
	xorps		$rndkey0,$inout0
	movdqu	0x30($inp),$inout3
	xorps		$rndkey0,$inout1
	movdqu	0x40($inp),$inout4
	aesdec		$rndkey1,$inout0
	pxor		$rndkey0,$inout2
	movdqu	0x50($inp),$inout5
	aesdec		$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	movdqu	0x60($inp),$inout6
	aesdec		$rndkey1,$inout2
	pxor		$rndkey0,$inout4
	movdqu	0x70($inp),$inout7
	aesdec		$rndkey1,$inout3
	pxor		$rndkey0,$inout5
	dec		$rounds
	aesdec		$rndkey1,$inout4
	pxor		$rndkey0,$inout6
	aesdec		$rndkey1,$inout5
	pxor		$rndkey0,$inout7
	$movkey		($key),$rndkey0
	aesdec		$rndkey1,$inout6
	aesdec		$rndkey1,$inout7
	$movkey		16($key),$rndkey1

	call		.Ldec_loop8_enter

	movups	($inp),$rndkey1		# re-load input
	movups	0x10($inp),$rndkey0
	xorps	(%rsp),$inout0		# ^= IV
	xorps	$rndkey1,$inout1
	movups	0x20($inp),$rndkey1
	xorps	$rndkey0,$inout2
	movups	0x30($inp),$rndkey0
	xorps	$rndkey1,$inout3
	movups	0x40($inp),$rndkey1
	xorps	$rndkey0,$inout4
	movups	0x50($inp),$rndkey0
	xorps	$rndkey1,$inout5
	movups	0x60($inp),$rndkey1
	xorps	$rndkey0,$inout6
	movups	0x70($inp),$rndkey0	# IV
	xorps	$rndkey1,$inout7
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout4,0x40($out)
	mov	$key_,$key		# restore $key
	movups	$inout5,0x50($out)
	lea	0x80($inp),$inp
	movups	$inout6,0x60($out)
	lea	0x70($out),$out
	sub	\$0x80,$len
	ja	.Lcbc_dec_loop8

	movaps	$inout7,$inout0
	movaps	$rndkey0,$iv
	add	\$0x70,$len
	jle	.Lcbc_dec_tail_collected
	movups	$inout0,($out)
	lea	1($rnds_,$rnds_),$rounds
	lea	0x10($out),$out
.Lcbc_dec_tail:
	movups	($inp),$inout0
	movaps	$inout0,$in0
	cmp	\$0x10,$len
	jbe	.Lcbc_dec_one

	movups	0x10($inp),$inout1
	movaps	$inout1,$in1
	cmp	\$0x20,$len
	jbe	.Lcbc_dec_two

	movups	0x20($inp),$inout2
	movaps	$inout2,$in2
	cmp	\$0x30,$len
	jbe	.Lcbc_dec_three

	movups	0x30($inp),$inout3
	cmp	\$0x40,$len
	jbe	.Lcbc_dec_four

	movups	0x40($inp),$inout4
	cmp	\$0x50,$len
	jbe	.Lcbc_dec_five

	movups	0x50($inp),$inout5
	cmp	\$0x60,$len
	jbe	.Lcbc_dec_six

	movups	0x60($inp),$inout6
	movaps	$iv,(%rsp)		# save IV
	call	_aesni_decrypt8
	movups	($inp),$rndkey1
	movups	0x10($inp),$rndkey0
	xorps	(%rsp),$inout0		# ^= IV
	xorps	$rndkey1,$inout1
	movups	0x20($inp),$rndkey1
	xorps	$rndkey0,$inout2
	movups	0x30($inp),$rndkey0
	xorps	$rndkey1,$inout3
	movups	0x40($inp),$rndkey1
	xorps	$rndkey0,$inout4
	movups	0x50($inp),$rndkey0
	xorps	$rndkey1,$inout5
	movups	0x60($inp),$iv		# IV
	xorps	$rndkey0,$inout6
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	lea	0x60($out),$out
	movaps	$inout6,$inout0
	sub	\$0x70,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_one:
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	$iv,$inout0
	movaps	$in0,$iv
	sub	\$0x10,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_two:
	xorps	$inout2,$inout2
	call	_aesni_decrypt3
	xorps	$iv,$inout0
	xorps	$in0,$inout1
	movups	$inout0,($out)
	movaps	$in1,$iv
	movaps	$inout1,$inout0
	lea	0x10($out),$out
	sub	\$0x20,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_three:
	call	_aesni_decrypt3
	xorps	$iv,$inout0
	xorps	$in0,$inout1
	movups	$inout0,($out)
	xorps	$in1,$inout2
	movups	$inout1,0x10($out)
	movaps	$in2,$iv
	movaps	$inout2,$inout0
	lea	0x20($out),$out
	sub	\$0x30,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_four:
	call	_aesni_decrypt4
	xorps	$iv,$inout0
	movups	0x30($inp),$iv
	xorps	$in0,$inout1
	movups	$inout0,($out)
	xorps	$in1,$inout2
	movups	$inout1,0x10($out)
	xorps	$in2,$inout3
	movups	$inout2,0x20($out)
	movaps	$inout3,$inout0
	lea	0x30($out),$out
	sub	\$0x40,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_five:
	xorps	$inout5,$inout5
	call	_aesni_decrypt6
	movups	0x10($inp),$rndkey1
	movups	0x20($inp),$rndkey0
	xorps	$iv,$inout0
	xorps	$in0,$inout1
	xorps	$rndkey1,$inout2
	movups	0x30($inp),$rndkey1
	xorps	$rndkey0,$inout3
	movups	0x40($inp),$iv
	xorps	$rndkey1,$inout4
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	lea	0x40($out),$out
	movaps	$inout4,$inout0
	sub	\$0x50,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_six:
	call	_aesni_decrypt6
	movups	0x10($inp),$rndkey1
	movups	0x20($inp),$rndkey0
	xorps	$iv,$inout0
	xorps	$in0,$inout1
	xorps	$rndkey1,$inout2
	movups	0x30($inp),$rndkey1
	xorps	$rndkey0,$inout3
	movups	0x40($inp),$rndkey0
	xorps	$rndkey1,$inout4
	movups	0x50($inp),$iv
	xorps	$rndkey0,$inout5
	movups	$inout0,($out)
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	lea	0x50($out),$out
	movaps	$inout5,$inout0
	sub	\$0x60,$len
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_tail_collected:
	and	\$15,$len
	movups	$iv,($ivp)
	jnz	.Lcbc_dec_tail_partial
	movups	$inout0,($out)
	jmp	.Lcbc_dec_ret
.align	16
.Lcbc_dec_tail_partial:
	movaps	$inout0,(%rsp)
	mov	\$16,%rcx
	mov	$out,%rdi
	sub	$len,%rcx
	lea	(%rsp),%rsi
	.long	0x9066A4F3	# rep movsb

.Lcbc_dec_ret:
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	0x20(%rsp),%xmm7
	movaps	0x30(%rsp),%xmm8
	movaps	0x40(%rsp),%xmm9
___
$code.=<<___;
	lea	(%rbp),%rsp
	pop	%rbp
.Lcbc_ret:
	ret
.size	${PREFIX}_cbc_encrypt,.-${PREFIX}_cbc_encrypt
___
} 
# int $PREFIX_set_[en|de]crypt_key (const unsigned char *userKey,
#				int bits, AES_KEY *key)
{ my ($inp,$bits,$key) = @_4args;
  $bits =~ s/%r/%e/;

$code.=<<___;
.globl	${PREFIX}_set_decrypt_key
.type	${PREFIX}_set_decrypt_key,\@abi-omnipotent
.align	16
${PREFIX}_set_decrypt_key:
	_CET_ENDBR
	sub	\$8,%rsp
	call	__aesni_set_encrypt_key
	shl	\$4,$bits		# rounds-1 after _aesni_set_encrypt_key
	test	%eax,%eax
	jnz	.Ldec_key_ret
	lea	16($key,$bits),$inp	# points at the end of key schedule

	$movkey	($key),%xmm0		# just swap
	$movkey	($inp),%xmm1
	$movkey	%xmm0,($inp)
	$movkey	%xmm1,($key)
	lea	16($key),$key
	lea	-16($inp),$inp

.Ldec_key_inverse:
	$movkey	($key),%xmm0		# swap and inverse
	$movkey	($inp),%xmm1
	aesimc	%xmm0,%xmm0
	aesimc	%xmm1,%xmm1
	lea	16($key),$key
	lea	-16($inp),$inp
	$movkey	%xmm0,16($inp)
	$movkey	%xmm1,-16($key)
	cmp	$key,$inp
	ja	.Ldec_key_inverse

	$movkey	($key),%xmm0		# inverse middle
	aesimc	%xmm0,%xmm0
	$movkey	%xmm0,($inp)
.Ldec_key_ret:
	add	\$8,%rsp
	ret
.LSEH_end_set_decrypt_key:
.size	${PREFIX}_set_decrypt_key,.-${PREFIX}_set_decrypt_key
___

# This is based on submission by
#
#	Huang Ying <ying.huang@intel.com>
#	Vinodh Gopal <vinodh.gopal@intel.com>
#	Kahraman Akdemir
#
# Aggressively optimized in respect to aeskeygenassist's critical path
# and is contained in %xmm0-5 to meet Win64 ABI requirement.
#
$code.=<<___;
.globl	${PREFIX}_set_encrypt_key
.type	${PREFIX}_set_encrypt_key,\@abi-omnipotent
.align	16
${PREFIX}_set_encrypt_key:
	_CET_ENDBR
__aesni_set_encrypt_key:
	sub	\$8,%rsp
	mov	\$-1,%rax
	test	$inp,$inp
	jz	.Lenc_key_ret
	test	$key,$key
	jz	.Lenc_key_ret

	movups	($inp),%xmm0		# pull first 128 bits of *userKey
	xorps	%xmm4,%xmm4		# low dword of xmm4 is assumed 0
	lea	16($key),%rax
	cmp	\$256,$bits
	je	.L14rounds
	cmp	\$192,$bits
	je	.L12rounds
	cmp	\$128,$bits
	jne	.Lbad_keybits

.L10rounds:
	mov	\$9,$bits			# 10 rounds for 128-bit key
	$movkey	%xmm0,($key)			# round 0
	aeskeygenassist	\$0x1,%xmm0,%xmm1	# round 1
	call		.Lkey_expansion_128_cold
	aeskeygenassist	\$0x2,%xmm0,%xmm1	# round 2
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x4,%xmm0,%xmm1	# round 3
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x8,%xmm0,%xmm1	# round 4
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x10,%xmm0,%xmm1	# round 5
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x20,%xmm0,%xmm1	# round 6
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x40,%xmm0,%xmm1	# round 7
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x80,%xmm0,%xmm1	# round 8
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x1b,%xmm0,%xmm1	# round 9
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x36,%xmm0,%xmm1	# round 10
	call		.Lkey_expansion_128
	$movkey	%xmm0,(%rax)
	mov	$bits,80(%rax)	# 240(%rdx)
	xor	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L12rounds:
	movq	16($inp),%xmm2			# remaining 1/3 of *userKey
	mov	\$11,$bits			# 12 rounds for 192
	$movkey	%xmm0,($key)			# round 0
	aeskeygenassist	\$0x1,%xmm2,%xmm1	# round 1,2
	call		.Lkey_expansion_192a_cold
	aeskeygenassist	\$0x2,%xmm2,%xmm1	# round 2,3
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x4,%xmm2,%xmm1	# round 4,5
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x8,%xmm2,%xmm1	# round 5,6
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x10,%xmm2,%xmm1	# round 7,8
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x20,%xmm2,%xmm1	# round 8,9
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x40,%xmm2,%xmm1	# round 10,11
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x80,%xmm2,%xmm1	# round 11,12
	call		.Lkey_expansion_192b
	$movkey	%xmm0,(%rax)
	mov	$bits,48(%rax)	# 240(%rdx)
	xor	%rax, %rax
	jmp	.Lenc_key_ret

.align	16
.L14rounds:
	movups	16($inp),%xmm2			# remaining half of *userKey
	mov	\$13,$bits			# 14 rounds for 256
	lea	16(%rax),%rax
	$movkey	%xmm0,($key)			# round 0
	$movkey	%xmm2,16($key)			# round 1
	aeskeygenassist	\$0x1,%xmm2,%xmm1	# round 2
	call		.Lkey_expansion_256a_cold
	aeskeygenassist	\$0x1,%xmm0,%xmm1	# round 3
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x2,%xmm2,%xmm1	# round 4
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x2,%xmm0,%xmm1	# round 5
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x4,%xmm2,%xmm1	# round 6
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x4,%xmm0,%xmm1	# round 7
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x8,%xmm2,%xmm1	# round 8
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x8,%xmm0,%xmm1	# round 9
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x10,%xmm2,%xmm1	# round 10
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x10,%xmm0,%xmm1	# round 11
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x20,%xmm2,%xmm1	# round 12
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x20,%xmm0,%xmm1	# round 13
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x40,%xmm2,%xmm1	# round 14
	call		.Lkey_expansion_256a
	$movkey	%xmm0,(%rax)
	mov	$bits,16(%rax)	# 240(%rdx)
	xor	%rax,%rax
	jmp	.Lenc_key_ret

.align	16
.Lbad_keybits:
	mov	\$-2,%rax
.Lenc_key_ret:
	add	\$8,%rsp
	ret
.LSEH_end_set_encrypt_key:

.align	16
.Lkey_expansion_128:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_128_cold:
	shufps	\$0b00010000,%xmm0,%xmm4
	xorps	%xmm4, %xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	xorps	%xmm4, %xmm0
	shufps	\$0b11111111,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm0
	ret

.align 16
.Lkey_expansion_192a:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_192a_cold:
	movaps	%xmm2, %xmm5
.Lkey_expansion_192b_warm:
	shufps	\$0b00010000,%xmm0,%xmm4
	movdqa	%xmm2,%xmm3
	xorps	%xmm4,%xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	pslldq	\$4,%xmm3
	xorps	%xmm4,%xmm0
	pshufd	\$0b01010101,%xmm1,%xmm1	# critical path
	pxor	%xmm3,%xmm2
	pxor	%xmm1,%xmm0
	pshufd	\$0b11111111,%xmm0,%xmm3
	pxor	%xmm3,%xmm2
	ret

.align 16
.Lkey_expansion_192b:
	movaps	%xmm0,%xmm3
	shufps	\$0b01000100,%xmm0,%xmm5
	$movkey	%xmm5,(%rax)
	shufps	\$0b01001110,%xmm2,%xmm3
	$movkey	%xmm3,16(%rax)
	lea	32(%rax),%rax
	jmp	.Lkey_expansion_192b_warm

.align	16
.Lkey_expansion_256a:
	$movkey	%xmm2,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_256a_cold:
	shufps	\$0b00010000,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	\$0b11111111,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm0
	ret

.align 16
.Lkey_expansion_256b:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax

	shufps	\$0b00010000,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	\$0b10001100,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	\$0b10101010,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm2
	ret
.size	${PREFIX}_set_encrypt_key,.-${PREFIX}_set_encrypt_key
.size	__aesni_set_encrypt_key,.-__aesni_set_encrypt_key
___
}

$code.=<<___;
.section .rodata
.align	64
.Lbswap_mask:
	.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.Lincrement32:
	.long	6,6,6,0
.Lincrement64:
	.long	1,0,0,0
.Lxts_magic:
	.long	0x87,0,1,0
.align	64
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
___
$code.=<<___ if ($PREFIX eq "aesni");
.type	ecb_se_handler,\@abi-omnipotent
.align	16
ecb_se_handler:
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

	mov	152($context),%rax	# pull context->Rsp

	jmp	.Lcommon_seh_tail
.size	ecb_se_handler,.-ecb_se_handler

.type	ccm64_se_handler,\@abi-omnipotent
.align	16
ccm64_se_handler:
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
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	lea	0(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$8,%ecx		# 4*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq
	lea	0x58(%rax),%rax		# adjust stack pointer

	jmp	.Lcommon_seh_tail
.size	ccm64_se_handler,.-ccm64_se_handler

.type	ctr32_se_handler,\@abi-omnipotent
.align	16
ctr32_se_handler:
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

	lea	.Lctr32_body(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<"prologue" label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	lea	.Lctr32_ret(%rip),%r10
	cmp	%r10,%rbx
	jae	.Lcommon_seh_tail

	lea	0x20(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

	jmp	.Lcommon_rbp_tail
.size	ctr32_se_handler,.-ctr32_se_handler

.type	xts_se_handler,\@abi-omnipotent
.align	16
xts_se_handler:
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
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	lea	0x60(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# & context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

	jmp	.Lcommon_rbp_tail
.size	xts_se_handler,.-xts_se_handler
___
$code.=<<___;
.type	cbc_se_handler,\@abi-omnipotent
.align	16
cbc_se_handler:
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

	mov	152($context),%rax	# pull context->Rsp
	mov	248($context),%rbx	# pull context->Rip

	lea	.Lcbc_decrypt(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<"prologue" label
	jb	.Lcommon_seh_tail

	lea	.Lcbc_decrypt_body(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<cbc_decrypt_body
	jb	.Lrestore_cbc_rax

	lea	.Lcbc_ret(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>="epilogue" label
	jae	.Lcommon_seh_tail

	lea	16(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$8,%ecx		# 4*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

.Lcommon_rbp_tail:
	mov	160($context),%rax	# pull context->Rbp
	mov	(%rax),%rbp		# restore saved %rbp
	lea	8(%rax),%rax		# adjust stack pointer
	mov	%rbp,160($context)	# restore context->Rbp
	jmp	.Lcommon_seh_tail

.Lrestore_cbc_rax:
	mov	120($context),%rax

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
.size	cbc_se_handler,.-cbc_se_handler

.section	.pdata
.align	4
___
$code.=<<___ if ($PREFIX eq "aesni");
	.rva	.LSEH_begin_aesni_ecb_encrypt
	.rva	.LSEH_end_aesni_ecb_encrypt
	.rva	.LSEH_info_ecb

	.rva	.LSEH_begin_aesni_ccm64_encrypt_blocks
	.rva	.LSEH_end_aesni_ccm64_encrypt_blocks
	.rva	.LSEH_info_ccm64_enc

	.rva	.LSEH_begin_aesni_ccm64_decrypt_blocks
	.rva	.LSEH_end_aesni_ccm64_decrypt_blocks
	.rva	.LSEH_info_ccm64_dec

	.rva	.LSEH_begin_aesni_ctr32_encrypt_blocks
	.rva	.LSEH_end_aesni_ctr32_encrypt_blocks
	.rva	.LSEH_info_ctr32

	.rva	.LSEH_begin_aesni_xts_encrypt
	.rva	.LSEH_end_aesni_xts_encrypt
	.rva	.LSEH_info_xts_enc

	.rva	.LSEH_begin_aesni_xts_decrypt
	.rva	.LSEH_end_aesni_xts_decrypt
	.rva	.LSEH_info_xts_dec
___
$code.=<<___;
	.rva	.LSEH_begin_${PREFIX}_cbc_encrypt
	.rva	.LSEH_end_${PREFIX}_cbc_encrypt
	.rva	.LSEH_info_cbc

	.rva	${PREFIX}_set_decrypt_key
	.rva	.LSEH_end_set_decrypt_key
	.rva	.LSEH_info_key

	.rva	${PREFIX}_set_encrypt_key
	.rva	.LSEH_end_set_encrypt_key
	.rva	.LSEH_info_key
.section	.xdata
.align	8
___
$code.=<<___ if ($PREFIX eq "aesni");
.LSEH_info_ecb:
	.byte	9,0,0,0
	.rva	ecb_se_handler
.LSEH_info_ccm64_enc:
	.byte	9,0,0,0
	.rva	ccm64_se_handler
	.rva	.Lccm64_enc_body,.Lccm64_enc_ret	# HandlerData[]
.LSEH_info_ccm64_dec:
	.byte	9,0,0,0
	.rva	ccm64_se_handler
	.rva	.Lccm64_dec_body,.Lccm64_dec_ret	# HandlerData[]
.LSEH_info_ctr32:
	.byte	9,0,0,0
	.rva	ctr32_se_handler
.LSEH_info_xts_enc:
	.byte	9,0,0,0
	.rva	xts_se_handler
	.rva	.Lxts_enc_body,.Lxts_enc_epilogue	# HandlerData[]
.LSEH_info_xts_dec:
	.byte	9,0,0,0
	.rva	xts_se_handler
	.rva	.Lxts_dec_body,.Lxts_dec_epilogue	# HandlerData[]
___
$code.=<<___;
.LSEH_info_cbc:
	.byte	9,0,0,0
	.rva	cbc_se_handler
.LSEH_info_key:
	.byte	0x01,0x04,0x01,0x00
	.byte	0x04,0x02,0x00,0x00	# sub rsp,8
___
}

sub rex {
  local *opcode=shift;
  my ($dst,$src)=@_;
  my $rex=0;

    $rex|=0x04			if($dst>=8);
    $rex|=0x01			if($src>=8);
    push @opcode,$rex|0x40	if($rex);
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;
