#!/usr/bin/env perl
#
# Copyright (c) 2010-2011 Intel Corp.
#   Author: Vinodh.Gopal@intel.com
#           Jim Guilford
#           Erdinc.Ozturk@intel.com
#           Maxim.Perminov@intel.com
#
# More information about algorithm used can be found at:
#   http://www.cse.buffalo.edu/srds2009/escs2009_submission_Gopal.pdf
#
# ====================================================================
# Copyright (c) 2011 The OpenSSL Project.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
#
# 3. All advertising materials mentioning features or use of this
#    software must display the following acknowledgment:
#    "This product includes software developed by the OpenSSL Project
#    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
#
# 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
#    endorse or promote products derived from this software without
#    prior written permission. For written permission, please contact
#    licensing@OpenSSL.org.
#
# 5. Products derived from this software may not be called "OpenSSL"
#    nor may "OpenSSL" appear in their names without prior written
#    permission of the OpenSSL Project.
#
# 6. Redistributions of any form whatsoever must retain the following
#    acknowledgment:
#    "This product includes software developed by the OpenSSL Project
#    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
#
# THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
# EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
# ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
# ====================================================================

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" $xlate $flavour $output";
*STDOUT=*OUT;

use strict;
my $code=".text\n\n";
my $m=0;

#
# Define x512 macros
#

#MULSTEP_512_ADD	MACRO	x7, x6, x5, x4, x3, x2, x1, x0, dst, src1, src2, add_src, tmp1, tmp2
#
# uses rax, rdx, and args
sub MULSTEP_512_ADD
{
 my ($x, $DST, $SRC2, $ASRC, $OP, $TMP)=@_;
 my @X=@$x;	# make a copy
$code.=<<___;
	 mov	(+8*0)($SRC2), %rax
	 mul	$OP			# rdx:rax = %OP * [0]
	 mov	($ASRC), $X[0]
	 add	%rax, $X[0]
	 adc	\$0, %rdx
	 mov	$X[0], $DST
___
for(my $i=1;$i<8;$i++) {
$code.=<<___;
	 mov	%rdx, $TMP

	 mov	(+8*$i)($SRC2), %rax
	 mul	$OP			# rdx:rax = %OP * [$i]
	 mov	(+8*$i)($ASRC), $X[$i]
	 add	%rax, $X[$i]
	 adc	\$0, %rdx
	 add	$TMP, $X[$i]
	 adc	\$0, %rdx
___
}
$code.=<<___;
	 mov	%rdx, $X[0]
___
}

#MULSTEP_512	MACRO	x7, x6, x5, x4, x3, x2, x1, x0, dst, src2, src1_val, tmp
#
# uses rax, rdx, and args
sub MULSTEP_512
{
 my ($x, $DST, $SRC2, $OP, $TMP)=@_;
 my @X=@$x;	# make a copy
$code.=<<___;
	 mov	(+8*0)($SRC2), %rax
	 mul	$OP			# rdx:rax = %OP * [0]
	 add	%rax, $X[0]
	 adc	\$0, %rdx
	 mov	$X[0], $DST
___
for(my $i=1;$i<8;$i++) {
$code.=<<___;
	 mov	%rdx, $TMP

	 mov	(+8*$i)($SRC2), %rax
	 mul	$OP			# rdx:rax = %OP * [$i]
	 add	%rax, $X[$i]
	 adc	\$0, %rdx
	 add	$TMP, $X[$i]
	 adc	\$0, %rdx
___
}
$code.=<<___;
	 mov	%rdx, $X[0]
___
}

#
# Swizzle Macros
#

# macro to copy data from flat space to swizzled table
#MACRO swizzle	pDst, pSrc, tmp1, tmp2
# pDst and pSrc are modified
sub swizzle
{
 my ($pDst, $pSrc, $cnt, $d0)=@_;
$code.=<<___;
	 mov	\$8, $cnt
loop_$m:
	 mov	($pSrc), $d0
	 mov	$d0#w, ($pDst)
	 shr	\$16, $d0
	 mov	$d0#w, (+64*1)($pDst)
	 shr	\$16, $d0
	 mov	$d0#w, (+64*2)($pDst)
	 shr	\$16, $d0
	 mov	$d0#w, (+64*3)($pDst)
	 lea	8($pSrc), $pSrc
	 lea	64*4($pDst), $pDst
	 dec	$cnt
	 jnz	loop_$m
___

 $m++;
}

# macro to copy data from swizzled table to  flat space
#MACRO unswizzle	pDst, pSrc, tmp*3
sub unswizzle
{
 my ($pDst, $pSrc, $cnt, $d0, $d1)=@_;
$code.=<<___;
	 mov	\$4, $cnt
loop_$m:
	 movzxw	(+64*3+256*0)($pSrc), $d0
	 movzxw	(+64*3+256*1)($pSrc), $d1
	 shl	\$16, $d0
	 shl	\$16, $d1
	 mov	(+64*2+256*0)($pSrc), $d0#w
	 mov	(+64*2+256*1)($pSrc), $d1#w
	 shl	\$16, $d0
	 shl	\$16, $d1
	 mov	(+64*1+256*0)($pSrc), $d0#w
	 mov	(+64*1+256*1)($pSrc), $d1#w
	 shl	\$16, $d0
	 shl	\$16, $d1
	 mov	(+64*0+256*0)($pSrc), $d0#w
	 mov	(+64*0+256*1)($pSrc), $d1#w
	 mov	$d0, (+8*0)($pDst)
	 mov	$d1, (+8*1)($pDst)
	 lea	256*2($pSrc), $pSrc
	 lea	8*2($pDst), $pDst
	 sub	\$1, $cnt
	 jnz	loop_$m
___

 $m++;
}

#
# Data Structures
#

# Reduce Data
#
#
# Offset  Value
# 0C0     Carries
# 0B8     X2[10]
# 0B0     X2[9]
# 0A8     X2[8]
# 0A0     X2[7]
# 098     X2[6]
# 090     X2[5]
# 088     X2[4]
# 080     X2[3]
# 078     X2[2]
# 070     X2[1]
# 068     X2[0]
# 060     X1[12]  P[10]
# 058     X1[11]  P[9]  Z[8]
# 050     X1[10]  P[8]  Z[7]
# 048     X1[9]   P[7]  Z[6]
# 040     X1[8]   P[6]  Z[5]
# 038     X1[7]   P[5]  Z[4]
# 030     X1[6]   P[4]  Z[3]
# 028     X1[5]   P[3]  Z[2]
# 020     X1[4]   P[2]  Z[1]
# 018     X1[3]   P[1]  Z[0]
# 010     X1[2]   P[0]  Y[2]
# 008     X1[1]   Q[1]  Y[1]
# 000     X1[0]   Q[0]  Y[0]

my $X1_offset           =  0;			# 13 qwords
my $X2_offset           =  $X1_offset + 13*8;			# 11 qwords
my $Carries_offset      =  $X2_offset + 11*8;			# 1 qword
my $Q_offset            =  0;			# 2 qwords
my $P_offset            =  $Q_offset + 2*8;			# 11 qwords
my $Y_offset            =  0;			# 3 qwords
my $Z_offset            =  $Y_offset + 3*8;			# 9 qwords

my $Red_Data_Size       =  $Carries_offset + 1*8;			# (25 qwords)

#
# Stack Frame
#
#
# offset	value
# ...		<old stack contents>
# ...
# 280		Garray

# 278		tmp16[15]
# ...		...
# 200		tmp16[0]

# 1F8		tmp[7]
# ...		...
# 1C0		tmp[0]

# 1B8		GT[7]
# ...		...
# 180		GT[0]

# 178		Reduce Data
# ...		...
# 0B8		Reduce Data
# 0B0		reserved
# 0A8		reserved
# 0A0		reserved
# 098		reserved
# 090		reserved
# 088		reduce result addr
# 080		exp[8]

# ...
# 048		exp[1]
# 040		exp[0]

# 038		reserved
# 030		loop_idx
# 028		pg
# 020		i
# 018		pData	; arg 4
# 010		pG	; arg 2
# 008		pResult	; arg 1
# 000		rsp	; stack pointer before subtract

my $rsp_offset          =  0;
my $pResult_offset      =  8*1 + $rsp_offset;
my $pG_offset           =  8*1 + $pResult_offset;
my $pData_offset        =  8*1 + $pG_offset;
my $i_offset            =  8*1 + $pData_offset;
my $pg_offset           =  8*1 + $i_offset;
my $loop_idx_offset     =  8*1 + $pg_offset;
my $reserved1_offset    =  8*1 + $loop_idx_offset;
my $exp_offset          =  8*1 + $reserved1_offset;
my $red_result_addr_offset=  8*9 + $exp_offset;
my $reserved2_offset    =  8*1 + $red_result_addr_offset;
my $Reduce_Data_offset  =  8*5 + $reserved2_offset;
my $GT_offset           =  $Red_Data_Size + $Reduce_Data_offset;
my $tmp_offset          =  8*8 + $GT_offset;
my $tmp16_offset        =  8*8 + $tmp_offset;
my $garray_offset       =  8*16 + $tmp16_offset;
my $mem_size            =  8*8*32 + $garray_offset;

#
# Offsets within Reduce Data
#
#
#	struct MODF_2FOLD_MONT_512_C1_DATA {
#	UINT64 t[8][8];
#	UINT64 m[8];
#	UINT64 m1[8]; /* 2^768 % m */
#	UINT64 m2[8]; /* 2^640 % m */
#	UINT64 k1[2]; /* (- 1/m) % 2^128 */
#	};

my $T                   =  0;
my $M                   =  512;			# = 8 * 8 * 8
my $M1                  =  576;			# = 8 * 8 * 9 /* += 8 * 8 */
my $M2                  =  640;			# = 8 * 8 * 10 /* += 8 * 8 */
my $K1                  =  704;			# = 8 * 8 * 11 /* += 8 * 8 */

#
#   FUNCTIONS
#

{{{
#
# MULADD_128x512 : Function to multiply 128-bits (2 qwords) by 512-bits (8 qwords)
#                       and add 512-bits (8 qwords)
#                       to get 640 bits (10 qwords)
# Input: 128-bit mul source: [rdi+8*1], rbp
#        512-bit mul source: [rsi+8*n]
#        512-bit add source: r15, r14, ..., r9, r8
# Output: r9, r8, r15, r14, r13, r12, r11, r10, [rcx+8*1], [rcx+8*0]
# Clobbers all regs except: rcx, rsi, rdi
$code.=<<___;
.type	MULADD_128x512,\@abi-omnipotent
.align	16
MULADD_128x512:
	_CET_ENDBR
___
	&MULSTEP_512([map("%r$_",(8..15))], "(+8*0)(%rcx)", "%rsi", "%rbp", "%rbx");
$code.=<<___;
	 mov	(+8*1)(%rdi), %rbp
___
	&MULSTEP_512([map("%r$_",(9..15,8))], "(+8*1)(%rcx)", "%rsi", "%rbp", "%rbx");
$code.=<<___;
	 ret
.size	MULADD_128x512,.-MULADD_128x512
___
}}}

{{{
#MULADD_256x512	MACRO	pDst, pA, pB, OP, TMP, X7, X6, X5, X4, X3, X2, X1, X0
#
# Inputs: pDst: Destination  (768 bits, 12 qwords)
#         pA:   Multiplicand (1024 bits, 16 qwords)
#         pB:   Multiplicand (512 bits, 8 qwords)
# Dst = Ah * B + Al
# where Ah is (in qwords) A[15:12] (256 bits) and Al is A[7:0] (512 bits)
# Results in X3 X2 X1 X0 X7 X6 X5 X4 Dst[3:0]
# Uses registers: arguments, RAX, RDX
sub MULADD_256x512
{
 my ($pDst, $pA, $pB, $OP, $TMP, $X)=@_;
$code.=<<___;
	mov	(+8*12)($pA), $OP
___
	&MULSTEP_512_ADD($X, "(+8*0)($pDst)", $pB, $pA, $OP, $TMP);
	push(@$X,shift(@$X));

$code.=<<___;
	 mov	(+8*13)($pA), $OP
___
	&MULSTEP_512($X, "(+8*1)($pDst)", $pB, $OP, $TMP);
	push(@$X,shift(@$X));

$code.=<<___;
	 mov	(+8*14)($pA), $OP
___
	&MULSTEP_512($X, "(+8*2)($pDst)", $pB, $OP, $TMP);
	push(@$X,shift(@$X));

$code.=<<___;
	 mov	(+8*15)($pA), $OP
___
	&MULSTEP_512($X, "(+8*3)($pDst)", $pB, $OP, $TMP);
	push(@$X,shift(@$X));
}

#
# mont_reduce(UINT64 *x,  /* 1024 bits, 16 qwords */
#	       UINT64 *m,  /*  512 bits,  8 qwords */
#	       MODF_2FOLD_MONT_512_C1_DATA *data,
#             UINT64 *r)  /*  512 bits,  8 qwords */
# Input:  x (number to be reduced): tmp16 (Implicit)
#         m (modulus):              [pM]  (Implicit)
#         data (reduce data):       [pData] (Implicit)
# Output: r (result):		     Address in [red_res_addr]
#         result also in: r9, r8, r15, r14, r13, r12, r11, r10

my @X=map("%r$_",(8..15));

$code.=<<___;
.type	mont_reduce,\@abi-omnipotent
.align	16
mont_reduce:
	_CET_ENDBR
___

my $STACK_DEPTH         =  8;
	#
	# X1 = Xh * M1 + Xl
$code.=<<___;
	 lea	(+$Reduce_Data_offset+$X1_offset+$STACK_DEPTH)(%rsp), %rdi			# pX1 (Dst) 769 bits, 13 qwords
	 mov	(+$pData_offset+$STACK_DEPTH)(%rsp), %rsi			# pM1 (Bsrc) 512 bits, 8 qwords
	 add	\$$M1, %rsi
	 lea	(+$tmp16_offset+$STACK_DEPTH)(%rsp), %rcx			# X (Asrc) 1024 bits, 16 qwords

___

	&MULADD_256x512("%rdi", "%rcx", "%rsi", "%rbp", "%rbx", \@X);	# rotates @X 4 times
	# results in r11, r10, r9, r8, r15, r14, r13, r12, X1[3:0]

$code.=<<___;
	 xor	%rax, %rax
	# X1 += xl
	 add	(+8*8)(%rcx), $X[4]
	 adc	(+8*9)(%rcx), $X[5]
	 adc	(+8*10)(%rcx), $X[6]
	 adc	(+8*11)(%rcx), $X[7]
	 adc	\$0, %rax
	# X1 is now rax, r11-r8, r15-r12, tmp16[3:0]

	#
	# check for carry ;; carry stored in rax
	 mov	$X[4], (+8*8)(%rdi)			# rdi points to X1
	 mov	$X[5], (+8*9)(%rdi)
	 mov	$X[6], %rbp
	 mov	$X[7], (+8*11)(%rdi)

	 mov	%rax, (+$Reduce_Data_offset+$Carries_offset+$STACK_DEPTH)(%rsp)

	 mov	(+8*0)(%rdi), $X[4]
	 mov	(+8*1)(%rdi), $X[5]
	 mov	(+8*2)(%rdi), $X[6]
	 mov	(+8*3)(%rdi), $X[7]

	# X1 is now stored in: X1[11], rbp, X1[9:8], r15-r8
	# rdi -> X1
	# rsi -> M1

	#
	# X2 = Xh * M2 + Xl
	# do first part (X2 = Xh * M2)
	 add	\$8*10, %rdi			# rdi -> pXh ; 128 bits, 2 qwords
				#        Xh is actually { [rdi+8*1], rbp }
	 add	\$`$M2-$M1`, %rsi			# rsi -> M2
	 lea	(+$Reduce_Data_offset+$X2_offset+$STACK_DEPTH)(%rsp), %rcx			# rcx -> pX2 ; 641 bits, 11 qwords
___
	unshift(@X,pop(@X));	unshift(@X,pop(@X));
$code.=<<___;

	 call	MULADD_128x512			# args in rcx, rdi / rbp, rsi, r15-r8
	# result in r9, r8, r15, r14, r13, r12, r11, r10, X2[1:0]
	 mov	(+$Reduce_Data_offset+$Carries_offset+$STACK_DEPTH)(%rsp), %rax

	# X2 += Xl
	 add	(+8*8-8*10)(%rdi), $X[6]		# (-8*10) is to adjust rdi -> Xh to Xl
	 adc	(+8*9-8*10)(%rdi), $X[7]
	 mov	$X[6], (+8*8)(%rcx)
	 mov	$X[7], (+8*9)(%rcx)

	 adc	%rax, %rax
	 mov	%rax, (+$Reduce_Data_offset+$Carries_offset+$STACK_DEPTH)(%rsp)

	 lea	(+$Reduce_Data_offset+$Q_offset+$STACK_DEPTH)(%rsp), %rdi			# rdi -> pQ ; 128 bits, 2 qwords
	 add	\$`$K1-$M2`, %rsi			# rsi -> pK1 ; 128 bits, 2 qwords

	# MUL_128x128t128	rdi, rcx, rsi	; Q = X2 * K1 (bottom half)
	# B1:B0 = rsi[1:0] = K1[1:0]
	# A1:A0 = rcx[1:0] = X2[1:0]
	# Result = rdi[1],rbp = Q[1],rbp
	 mov	(%rsi), %r8			# B0
	 mov	(+8*1)(%rsi), %rbx			# B1

	 mov	(%rcx), %rax			# A0
	 mul	%r8			# B0
	 mov	%rax, %rbp
	 mov	%rdx, %r9

	 mov	(+8*1)(%rcx), %rax			# A1
	 mul	%r8			# B0
	 add	%rax, %r9

	 mov	(%rcx), %rax			# A0
	 mul	%rbx			# B1
	 add	%rax, %r9

	 mov	%r9, (+8*1)(%rdi)
	# end MUL_128x128t128

	 sub	\$`$K1-$M`, %rsi

	 mov	(%rcx), $X[6]
	 mov	(+8*1)(%rcx), $X[7]			# r9:r8 = X2[1:0]

	 call	MULADD_128x512			# args in rcx, rdi / rbp, rsi, r15-r8
	# result in r9, r8, r15, r14, r13, r12, r11, r10, X2[1:0]

	# load first half of m to rdx, rdi, rbx, rax
	# moved this here for efficiency
	 mov	(+8*0)(%rsi), %rax
	 mov	(+8*1)(%rsi), %rbx
	 mov	(+8*2)(%rsi), %rdi
	 mov	(+8*3)(%rsi), %rdx

	# continue with reduction
	 mov	(+$Reduce_Data_offset+$Carries_offset+$STACK_DEPTH)(%rsp), %rbp

	 add	(+8*8)(%rcx), $X[6]
	 adc	(+8*9)(%rcx), $X[7]

	#accumulate the final carry to rbp
	 adc	%rbp, %rbp

	# Add in overflow corrections: R = (X2>>128) += T[overflow]
	# R = {r9, r8, r15, r14, ..., r10}
	 shl	\$3, %rbp
	 mov	(+$pData_offset+$STACK_DEPTH)(%rsp), %rcx			# rsi -> Data (and points to T)
	 add	%rcx, %rbp			# pT ; 512 bits, 8 qwords, spread out

	# rsi will be used to generate a mask after the addition
	 xor	%rsi, %rsi

	 add	(+8*8*0)(%rbp), $X[0]
	 adc	(+8*8*1)(%rbp), $X[1]
	 adc	(+8*8*2)(%rbp), $X[2]
	 adc	(+8*8*3)(%rbp), $X[3]
	 adc	(+8*8*4)(%rbp), $X[4]
	 adc	(+8*8*5)(%rbp), $X[5]
	 adc	(+8*8*6)(%rbp), $X[6]
	 adc	(+8*8*7)(%rbp), $X[7]

	# if there is a carry:	rsi = 0xFFFFFFFFFFFFFFFF
	# if carry is clear:	rsi = 0x0000000000000000
	 sbb	\$0, %rsi

	# if carry is clear, subtract 0. Otherwise, subtract 256 bits of m
	 and	%rsi, %rax
	 and	%rsi, %rbx
	 and	%rsi, %rdi
	 and	%rsi, %rdx

	 mov	\$1, %rbp
	 sub	%rax, $X[0]
	 sbb	%rbx, $X[1]
	 sbb	%rdi, $X[2]
	 sbb	%rdx, $X[3]

	# if there is a borrow:		rbp = 0
	# if there is no borrow:	rbp = 1
	# this is used to save the borrows in between the first half and the 2nd half of the subtraction of m
	 sbb	\$0, %rbp

	#load second half of m to rdx, rdi, rbx, rax

	 add	\$$M, %rcx
	 mov	(+8*4)(%rcx), %rax
	 mov	(+8*5)(%rcx), %rbx
	 mov	(+8*6)(%rcx), %rdi
	 mov	(+8*7)(%rcx), %rdx

	# use the rsi mask as before
	# if carry is clear, subtract 0. Otherwise, subtract 256 bits of m
	 and	%rsi, %rax
	 and	%rsi, %rbx
	 and	%rsi, %rdi
	 and	%rsi, %rdx

	# if rbp = 0, there was a borrow before, it is moved to the carry flag
	# if rbp = 1, there was not a borrow before, carry flag is cleared
	 sub	\$1, %rbp

	 sbb	%rax, $X[4]
	 sbb	%rbx, $X[5]
	 sbb	%rdi, $X[6]
	 sbb	%rdx, $X[7]

	# write R back to memory

	 mov	(+$red_result_addr_offset+$STACK_DEPTH)(%rsp), %rsi
	 mov	$X[0], (+8*0)(%rsi)
	 mov	$X[1], (+8*1)(%rsi)
	 mov	$X[2], (+8*2)(%rsi)
	 mov	$X[3], (+8*3)(%rsi)
	 mov	$X[4], (+8*4)(%rsi)
	 mov	$X[5], (+8*5)(%rsi)
	 mov	$X[6], (+8*6)(%rsi)
	 mov	$X[7], (+8*7)(%rsi)

	 ret
.size	mont_reduce,.-mont_reduce
___
}}}

{{{
#MUL_512x512	MACRO	pDst, pA, pB, x7, x6, x5, x4, x3, x2, x1, x0, tmp*2
#
# Inputs: pDst: Destination  (1024 bits, 16 qwords)
#         pA:   Multiplicand (512 bits, 8 qwords)
#         pB:   Multiplicand (512 bits, 8 qwords)
# Uses registers rax, rdx, args
#   B operand in [pB] and also in x7...x0
sub MUL_512x512
{
 my ($pDst, $pA, $pB, $x, $OP, $TMP, $pDst_o)=@_;
 my ($pDst,  $pDst_o) = ($pDst =~ m/([^+]*)\+?(.*)?/);
 my @X=@$x;	# make a copy

$code.=<<___;
	 mov	(+8*0)($pA), $OP

	 mov	$X[0], %rax
	 mul	$OP			# rdx:rax = %OP * [0]
	 mov	%rax, (+$pDst_o+8*0)($pDst)
	 mov	%rdx, $X[0]
___
for(my $i=1;$i<8;$i++) {
$code.=<<___;
	 mov	$X[$i], %rax
	 mul	$OP			# rdx:rax = %OP * [$i]
	 add	%rax, $X[$i-1]
	 adc	\$0, %rdx
	 mov	%rdx, $X[$i]
___
}

for(my $i=1;$i<8;$i++) {
$code.=<<___;
	 mov	(+8*$i)($pA), $OP
___

	&MULSTEP_512(\@X, "(+$pDst_o+8*$i)($pDst)", $pB, $OP, $TMP);
	push(@X,shift(@X));
}

$code.=<<___;
	 mov	$X[0], (+$pDst_o+8*8)($pDst)
	 mov	$X[1], (+$pDst_o+8*9)($pDst)
	 mov	$X[2], (+$pDst_o+8*10)($pDst)
	 mov	$X[3], (+$pDst_o+8*11)($pDst)
	 mov	$X[4], (+$pDst_o+8*12)($pDst)
	 mov	$X[5], (+$pDst_o+8*13)($pDst)
	 mov	$X[6], (+$pDst_o+8*14)($pDst)
	 mov	$X[7], (+$pDst_o+8*15)($pDst)
___
}

#
# mont_mul_a3b : subroutine to compute (Src1 * Src2) % M (all 512-bits)
# Input:  src1: Address of source 1: rdi
#         src2: Address of source 2: rsi
# Output: dst:  Address of destination: [red_res_addr]
#    src2 and result also in: r9, r8, r15, r14, r13, r12, r11, r10
# Temp:   Clobbers [tmp16], all registers
$code.=<<___;
.type	mont_mul_a3b,\@abi-omnipotent
.align	16
mont_mul_a3b:
	_CET_ENDBR
	#
	# multiply tmp = src1 * src2
	# For multiply: dst = rcx, src1 = rdi, src2 = rsi
	# stack depth is extra 8 from call
___
	&MUL_512x512("%rsp+$tmp16_offset+8", "%rdi", "%rsi", [map("%r$_",(10..15,8..9))], "%rbp", "%rbx");
$code.=<<___;
	#
	# Dst = tmp % m
	# Call reduce(tmp, m, data, dst)

	# tail recursion optimization: jmp to mont_reduce and return from there
	 jmp	mont_reduce
	# call	mont_reduce
	# ret
.size	mont_mul_a3b,.-mont_mul_a3b
___
}}}

{{{
#SQR_512 MACRO pDest, pA, x7, x6, x5, x4, x3, x2, x1, x0, tmp*4
#
# Input in memory [pA] and also in x7...x0
# Uses all argument registers plus rax and rdx
#
# This version computes all of the off-diagonal terms into memory,
# and then it adds in the diagonal terms

sub SQR_512
{
 my ($pDst, $pA, $x, $A, $tmp, $x7, $x6, $pDst_o)=@_;
 my ($pDst,  $pDst_o) = ($pDst =~ m/([^+]*)\+?(.*)?/);
 my @X=@$x;	# make a copy
$code.=<<___;
	# ------------------
	# first pass 01...07
	# ------------------
	 mov	$X[0], $A

	 mov	$X[1],%rax
	 mul	$A
	 mov	%rax, (+$pDst_o+8*1)($pDst)
___
for(my $i=2;$i<8;$i++) {
$code.=<<___;
	 mov	%rdx, $X[$i-2]
	 mov	$X[$i],%rax
	 mul	$A
	 add	%rax, $X[$i-2]
	 adc	\$0, %rdx
___
}
$code.=<<___;
	 mov	%rdx, $x7

	 mov	$X[0], (+$pDst_o+8*2)($pDst)

	# ------------------
	# second pass 12...17
	# ------------------

	 mov	(+8*1)($pA), $A

	 mov	(+8*2)($pA),%rax
	 mul	$A
	 add	%rax, $X[1]
	 adc	\$0, %rdx
	 mov	$X[1], (+$pDst_o+8*3)($pDst)

	 mov	%rdx, $X[0]
	 mov	(+8*3)($pA),%rax
	 mul	$A
	 add	%rax, $X[2]
	 adc	\$0, %rdx
	 add	$X[0], $X[2]
	 adc	\$0, %rdx
	 mov	$X[2], (+$pDst_o+8*4)($pDst)

	 mov	%rdx, $X[0]
	 mov	(+8*4)($pA),%rax
	 mul	$A
	 add	%rax, $X[3]
	 adc	\$0, %rdx
	 add	$X[0], $X[3]
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	(+8*5)($pA),%rax
	 mul	$A
	 add	%rax, $X[4]
	 adc	\$0, %rdx
	 add	$X[0], $X[4]
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	$X[6],%rax
	 mul	$A
	 add	%rax, $X[5]
	 adc	\$0, %rdx
	 add	$X[0], $X[5]
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $x7
	 adc	\$0, %rdx
	 add	$X[0], $x7
	 adc	\$0, %rdx

	 mov	%rdx, $X[1]

	# ------------------
	# third pass 23...27
	# ------------------
	 mov	(+8*2)($pA), $A

	 mov	(+8*3)($pA),%rax
	 mul	$A
	 add	%rax, $X[3]
	 adc	\$0, %rdx
	 mov	$X[3], (+$pDst_o+8*5)($pDst)

	 mov	%rdx, $X[0]
	 mov	(+8*4)($pA),%rax
	 mul	$A
	 add	%rax, $X[4]
	 adc	\$0, %rdx
	 add	$X[0], $X[4]
	 adc	\$0, %rdx
	 mov	$X[4], (+$pDst_o+8*6)($pDst)

	 mov	%rdx, $X[0]
	 mov	(+8*5)($pA),%rax
	 mul	$A
	 add	%rax, $X[5]
	 adc	\$0, %rdx
	 add	$X[0], $X[5]
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	$X[6],%rax
	 mul	$A
	 add	%rax, $x7
	 adc	\$0, %rdx
	 add	$X[0], $x7
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $X[1]
	 adc	\$0, %rdx
	 add	$X[0], $X[1]
	 adc	\$0, %rdx

	 mov	%rdx, $X[2]

	# ------------------
	# fourth pass 34...37
	# ------------------

	 mov	(+8*3)($pA), $A

	 mov	(+8*4)($pA),%rax
	 mul	$A
	 add	%rax, $X[5]
	 adc	\$0, %rdx
	 mov	$X[5], (+$pDst_o+8*7)($pDst)

	 mov	%rdx, $X[0]
	 mov	(+8*5)($pA),%rax
	 mul	$A
	 add	%rax, $x7
	 adc	\$0, %rdx
	 add	$X[0], $x7
	 adc	\$0, %rdx
	 mov	$x7, (+$pDst_o+8*8)($pDst)

	 mov	%rdx, $X[0]
	 mov	$X[6],%rax
	 mul	$A
	 add	%rax, $X[1]
	 adc	\$0, %rdx
	 add	$X[0], $X[1]
	 adc	\$0, %rdx

	 mov	%rdx, $X[0]
	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $X[2]
	 adc	\$0, %rdx
	 add	$X[0], $X[2]
	 adc	\$0, %rdx

	 mov	%rdx, $X[5]

	# ------------------
	# fifth pass 45...47
	# ------------------
	 mov	(+8*4)($pA), $A

	 mov	(+8*5)($pA),%rax
	 mul	$A
	 add	%rax, $X[1]
	 adc	\$0, %rdx
	 mov	$X[1], (+$pDst_o+8*9)($pDst)

	 mov	%rdx, $X[0]
	 mov	$X[6],%rax
	 mul	$A
	 add	%rax, $X[2]
	 adc	\$0, %rdx
	 add	$X[0], $X[2]
	 adc	\$0, %rdx
	 mov	$X[2], (+$pDst_o+8*10)($pDst)

	 mov	%rdx, $X[0]
	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $X[5]
	 adc	\$0, %rdx
	 add	$X[0], $X[5]
	 adc	\$0, %rdx

	 mov	%rdx, $X[1]

	# ------------------
	# sixth pass 56...57
	# ------------------
	 mov	(+8*5)($pA), $A

	 mov	$X[6],%rax
	 mul	$A
	 add	%rax, $X[5]
	 adc	\$0, %rdx
	 mov	$X[5], (+$pDst_o+8*11)($pDst)

	 mov	%rdx, $X[0]
	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $X[1]
	 adc	\$0, %rdx
	 add	$X[0], $X[1]
	 adc	\$0, %rdx
	 mov	$X[1], (+$pDst_o+8*12)($pDst)

	 mov	%rdx, $X[2]

	# ------------------
	# seventh pass 67
	# ------------------
	 mov	$X[6], $A

	 mov	$X[7],%rax
	 mul	$A
	 add	%rax, $X[2]
	 adc	\$0, %rdx
	 mov	$X[2], (+$pDst_o+8*13)($pDst)

	 mov	%rdx, (+$pDst_o+8*14)($pDst)

	# start finalize (add	in squares, and double off-terms)
	 mov	(+$pDst_o+8*1)($pDst), $X[0]
	 mov	(+$pDst_o+8*2)($pDst), $X[1]
	 mov	(+$pDst_o+8*3)($pDst), $X[2]
	 mov	(+$pDst_o+8*4)($pDst), $X[3]
	 mov	(+$pDst_o+8*5)($pDst), $X[4]
	 mov	(+$pDst_o+8*6)($pDst), $X[5]

	 mov	(+8*3)($pA), %rax
	 mul	%rax
	 mov	%rax, $x6
	 mov	%rdx, $X[6]

	 add	$X[0], $X[0]
	 adc	$X[1], $X[1]
	 adc	$X[2], $X[2]
	 adc	$X[3], $X[3]
	 adc	$X[4], $X[4]
	 adc	$X[5], $X[5]
	 adc	\$0, $X[6]

	 mov	(+8*0)($pA), %rax
	 mul	%rax
	 mov	%rax, (+$pDst_o+8*0)($pDst)
	 mov	%rdx, $A

	 mov	(+8*1)($pA), %rax
	 mul	%rax

	 add	$A, $X[0]
	 adc	%rax, $X[1]
	 adc	\$0, %rdx

	 mov	%rdx, $A
	 mov	$X[0], (+$pDst_o+8*1)($pDst)
	 mov	$X[1], (+$pDst_o+8*2)($pDst)

	 mov	(+8*2)($pA), %rax
	 mul	%rax

	 add	$A, $X[2]
	 adc	%rax, $X[3]
	 adc	\$0, %rdx

	 mov	%rdx, $A

	 mov	$X[2], (+$pDst_o+8*3)($pDst)
	 mov	$X[3], (+$pDst_o+8*4)($pDst)

	 xor	$tmp, $tmp
	 add	$A, $X[4]
	 adc	$x6, $X[5]
	 adc	\$0, $tmp

	 mov	$X[4], (+$pDst_o+8*5)($pDst)
	 mov	$X[5], (+$pDst_o+8*6)($pDst)

	# %%tmp has 0/1 in column 7
	# %%A6 has a full value in column 7

	 mov	(+$pDst_o+8*7)($pDst), $X[0]
	 mov	(+$pDst_o+8*8)($pDst), $X[1]
	 mov	(+$pDst_o+8*9)($pDst), $X[2]
	 mov	(+$pDst_o+8*10)($pDst), $X[3]
	 mov	(+$pDst_o+8*11)($pDst), $X[4]
	 mov	(+$pDst_o+8*12)($pDst), $X[5]
	 mov	(+$pDst_o+8*13)($pDst), $x6
	 mov	(+$pDst_o+8*14)($pDst), $x7

	 mov	$X[7], %rax
	 mul	%rax
	 mov	%rax, $X[7]
	 mov	%rdx, $A

	 add	$X[0], $X[0]
	 adc	$X[1], $X[1]
	 adc	$X[2], $X[2]
	 adc	$X[3], $X[3]
	 adc	$X[4], $X[4]
	 adc	$X[5], $X[5]
	 adc	$x6, $x6
	 adc	$x7, $x7
	 adc	\$0, $A

	 add	$tmp, $X[0]

	 mov	(+8*4)($pA), %rax
	 mul	%rax

	 add	$X[6], $X[0]
	 adc	%rax, $X[1]
	 adc	\$0, %rdx

	 mov	%rdx, $tmp

	 mov	$X[0], (+$pDst_o+8*7)($pDst)
	 mov	$X[1], (+$pDst_o+8*8)($pDst)

	 mov	(+8*5)($pA), %rax
	 mul	%rax

	 add	$tmp, $X[2]
	 adc	%rax, $X[3]
	 adc	\$0, %rdx

	 mov	%rdx, $tmp

	 mov	$X[2], (+$pDst_o+8*9)($pDst)
	 mov	$X[3], (+$pDst_o+8*10)($pDst)

	 mov	(+8*6)($pA), %rax
	 mul	%rax

	 add	$tmp, $X[4]
	 adc	%rax, $X[5]
	 adc	\$0, %rdx

	 mov	$X[4], (+$pDst_o+8*11)($pDst)
	 mov	$X[5], (+$pDst_o+8*12)($pDst)

	 add	%rdx, $x6
	 adc	$X[7], $x7
	 adc	\$0, $A

	 mov	$x6, (+$pDst_o+8*13)($pDst)
	 mov	$x7, (+$pDst_o+8*14)($pDst)
	 mov	$A, (+$pDst_o+8*15)($pDst)
___
}

#
# sqr_reduce: subroutine to compute Result = reduce(Result * Result)
#
# input and result also in: r9, r8, r15, r14, r13, r12, r11, r10
#
$code.=<<___;
.type	sqr_reduce,\@abi-omnipotent
.align	16
sqr_reduce:
	_CET_ENDBR
	 mov	(+$pResult_offset+8)(%rsp), %rcx
___
	&SQR_512("%rsp+$tmp16_offset+8", "%rcx", [map("%r$_",(10..15,8..9))], "%rbx", "%rbp", "%rsi", "%rdi");
$code.=<<___;
	# tail recursion optimization: jmp to mont_reduce and return from there
	 jmp	mont_reduce
	# call	mont_reduce
	# ret
.size	sqr_reduce,.-sqr_reduce
___
}}}

#
# MAIN FUNCTION
#

#mod_exp_512(UINT64 *result, /* 512 bits, 8 qwords */
#           UINT64 *g,   /* 512 bits, 8 qwords */
#           UINT64 *exp, /* 512 bits, 8 qwords */
#           struct mod_ctx_512 *data)

# window size = 5
# table size = 2^5 = 32
#table_entries	equ	32
#table_size	equ	table_entries * 8
$code.=<<___;
.globl	mod_exp_512
.type	mod_exp_512,\@function,4
mod_exp_512:
	_CET_ENDBR
	 push	%rbp
	 push	%rbx
	 push	%r12
	 push	%r13
	 push	%r14
	 push	%r15

	# adjust stack down and then align it with cache boundary
	 mov	%rsp, %r8
	 sub	\$$mem_size, %rsp
	 and	\$-64, %rsp

	# store previous stack pointer and arguments
	 mov	%r8, (+$rsp_offset)(%rsp)
	 mov	%rdi, (+$pResult_offset)(%rsp)
	 mov	%rsi, (+$pG_offset)(%rsp)
	 mov	%rcx, (+$pData_offset)(%rsp)
.Lbody:
	# transform g into montgomery space
	# GT = reduce(g * C2) = reduce(g * (2^256))
	# reduce expects to have the input in [tmp16]
	 pxor	%xmm4, %xmm4
	 movdqu	(+16*0)(%rsi), %xmm0
	 movdqu	(+16*1)(%rsi), %xmm1
	 movdqu	(+16*2)(%rsi), %xmm2
	 movdqu	(+16*3)(%rsi), %xmm3
	 movdqa	%xmm4, (+$tmp16_offset+16*0)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*1)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*6)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*7)(%rsp)
	 movdqa	%xmm0, (+$tmp16_offset+16*2)(%rsp)
	 movdqa	%xmm1, (+$tmp16_offset+16*3)(%rsp)
	 movdqa	%xmm2, (+$tmp16_offset+16*4)(%rsp)
	 movdqa	%xmm3, (+$tmp16_offset+16*5)(%rsp)

	# load pExp before rdx gets blown away
	 movdqu	(+16*0)(%rdx), %xmm0
	 movdqu	(+16*1)(%rdx), %xmm1
	 movdqu	(+16*2)(%rdx), %xmm2
	 movdqu	(+16*3)(%rdx), %xmm3

	 lea	(+$GT_offset)(%rsp), %rbx
	 mov	%rbx, (+$red_result_addr_offset)(%rsp)
	 call	mont_reduce

	# Initialize tmp = C
	 lea	(+$tmp_offset)(%rsp), %rcx
	 xor	%rax, %rax
	 mov	%rax, (+8*0)(%rcx)
	 mov	%rax, (+8*1)(%rcx)
	 mov	%rax, (+8*3)(%rcx)
	 mov	%rax, (+8*4)(%rcx)
	 mov	%rax, (+8*5)(%rcx)
	 mov	%rax, (+8*6)(%rcx)
	 mov	%rax, (+8*7)(%rcx)
	 mov	%rax, (+$exp_offset+8*8)(%rsp)
	 movq	\$1, (+8*2)(%rcx)

	 lea	(+$garray_offset)(%rsp), %rbp
	 mov	%rcx, %rsi			# pTmp
	 mov	%rbp, %rdi			# Garray[][0]
___

	&swizzle("%rdi", "%rcx", "%rax", "%rbx");

	# for (rax = 31; rax != 0; rax--) {
	#     tmp = reduce(tmp * G)
	#     swizzle(pg, tmp);
	#     pg += 2; }
$code.=<<___;
	 mov	\$31, %rax
	 mov	%rax, (+$i_offset)(%rsp)
	 mov	%rbp, (+$pg_offset)(%rsp)
	# rsi -> pTmp
	 mov	%rsi, (+$red_result_addr_offset)(%rsp)
	 mov	(+8*0)(%rsi), %r10
	 mov	(+8*1)(%rsi), %r11
	 mov	(+8*2)(%rsi), %r12
	 mov	(+8*3)(%rsi), %r13
	 mov	(+8*4)(%rsi), %r14
	 mov	(+8*5)(%rsi), %r15
	 mov	(+8*6)(%rsi), %r8
	 mov	(+8*7)(%rsi), %r9
init_loop:
	 lea	(+$GT_offset)(%rsp), %rdi
	 call	mont_mul_a3b
	 lea	(+$tmp_offset)(%rsp), %rsi
	 mov	(+$pg_offset)(%rsp), %rbp
	 add	\$2, %rbp
	 mov	%rbp, (+$pg_offset)(%rsp)
	 mov	%rsi, %rcx			# rcx = rsi = addr of tmp
___

	&swizzle("%rbp", "%rcx", "%rax", "%rbx");
$code.=<<___;
	 mov	(+$i_offset)(%rsp), %rax
	 sub	\$1, %rax
	 mov	%rax, (+$i_offset)(%rsp)
	 jne	init_loop

	#
	# Copy exponent onto stack
	 movdqa	%xmm0, (+$exp_offset+16*0)(%rsp)
	 movdqa	%xmm1, (+$exp_offset+16*1)(%rsp)
	 movdqa	%xmm2, (+$exp_offset+16*2)(%rsp)
	 movdqa	%xmm3, (+$exp_offset+16*3)(%rsp)


	#
	# Do exponentiation
	# Initialize result to G[exp{511:507}]
	 mov	(+$exp_offset+62)(%rsp), %eax
	 mov	%rax, %rdx
	 shr	\$11, %rax
	 and	\$0x07FF, %edx
	 mov	%edx, (+$exp_offset+62)(%rsp)
	 lea	(+$garray_offset)(%rsp,%rax,2), %rsi
	 mov	(+$pResult_offset)(%rsp), %rdx
___

	&unswizzle("%rdx", "%rsi", "%rbp", "%rbx", "%rax");

	#
	# Loop variables
	# rcx = [loop_idx] = index: 510-5 to 0 by 5
$code.=<<___;
	 movq	\$505, (+$loop_idx_offset)(%rsp)

	 mov	(+$pResult_offset)(%rsp), %rcx
	 mov	%rcx, (+$red_result_addr_offset)(%rsp)
	 mov	(+8*0)(%rcx), %r10
	 mov	(+8*1)(%rcx), %r11
	 mov	(+8*2)(%rcx), %r12
	 mov	(+8*3)(%rcx), %r13
	 mov	(+8*4)(%rcx), %r14
	 mov	(+8*5)(%rcx), %r15
	 mov	(+8*6)(%rcx), %r8
	 mov	(+8*7)(%rcx), %r9
	 jmp	sqr_2

main_loop_a3b:
	 call	sqr_reduce
	 call	sqr_reduce
	 call	sqr_reduce
sqr_2:
	 call	sqr_reduce
	 call	sqr_reduce

	#
	# Do multiply, first look up proper value in Garray
	 mov	(+$loop_idx_offset)(%rsp), %rcx			# bit index
	 mov	%rcx, %rax
	 shr	\$4, %rax			# rax is word pointer
	 mov	(+$exp_offset)(%rsp,%rax,2), %edx
	 and	\$15, %rcx
	 shrq	%cl, %rdx
	 and	\$0x1F, %rdx

	 lea	(+$garray_offset)(%rsp,%rdx,2), %rsi
	 lea	(+$tmp_offset)(%rsp), %rdx
	 mov	%rdx, %rdi
___

	&unswizzle("%rdx", "%rsi", "%rbp", "%rbx", "%rax");
	# rdi = tmp = pG

	#
	# Call mod_mul_a1(pDst,  pSrc1, pSrc2, pM, pData)
	#                 result result pG     M   Data
$code.=<<___;
	 mov	(+$pResult_offset)(%rsp), %rsi
	 call	mont_mul_a3b

	#
	# finish loop
	 mov	(+$loop_idx_offset)(%rsp), %rcx
	 sub	\$5, %rcx
	 mov	%rcx, (+$loop_idx_offset)(%rsp)
	 jge	main_loop_a3b

	#

end_main_loop_a3b:
	# transform result out of Montgomery space
	# result = reduce(result)
	 mov	(+$pResult_offset)(%rsp), %rdx
	 pxor	%xmm4, %xmm4
	 movdqu	(+16*0)(%rdx), %xmm0
	 movdqu	(+16*1)(%rdx), %xmm1
	 movdqu	(+16*2)(%rdx), %xmm2
	 movdqu	(+16*3)(%rdx), %xmm3
	 movdqa	%xmm4, (+$tmp16_offset+16*4)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*5)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*6)(%rsp)
	 movdqa	%xmm4, (+$tmp16_offset+16*7)(%rsp)
	 movdqa	%xmm0, (+$tmp16_offset+16*0)(%rsp)
	 movdqa	%xmm1, (+$tmp16_offset+16*1)(%rsp)
	 movdqa	%xmm2, (+$tmp16_offset+16*2)(%rsp)
	 movdqa	%xmm3, (+$tmp16_offset+16*3)(%rsp)
	 call	mont_reduce

	# If result > m, subtract m
	# load result into r15:r8
	 mov	(+$pResult_offset)(%rsp), %rax
	 mov	(+8*0)(%rax), %r8
	 mov	(+8*1)(%rax), %r9
	 mov	(+8*2)(%rax), %r10
	 mov	(+8*3)(%rax), %r11
	 mov	(+8*4)(%rax), %r12
	 mov	(+8*5)(%rax), %r13
	 mov	(+8*6)(%rax), %r14
	 mov	(+8*7)(%rax), %r15

	# subtract m
	 mov	(+$pData_offset)(%rsp), %rbx
	 add	\$$M, %rbx

	 sub	(+8*0)(%rbx), %r8
	 sbb	(+8*1)(%rbx), %r9
	 sbb	(+8*2)(%rbx), %r10
	 sbb	(+8*3)(%rbx), %r11
	 sbb	(+8*4)(%rbx), %r12
	 sbb	(+8*5)(%rbx), %r13
	 sbb	(+8*6)(%rbx), %r14
	 sbb	(+8*7)(%rbx), %r15

	# if Carry is clear, replace result with difference
	 mov	(+8*0)(%rax), %rsi
	 mov	(+8*1)(%rax), %rdi
	 mov	(+8*2)(%rax), %rcx
	 mov	(+8*3)(%rax), %rdx
	 cmovnc	%r8, %rsi
	 cmovnc	%r9, %rdi
	 cmovnc	%r10, %rcx
	 cmovnc	%r11, %rdx
	 mov	%rsi, (+8*0)(%rax)
	 mov	%rdi, (+8*1)(%rax)
	 mov	%rcx, (+8*2)(%rax)
	 mov	%rdx, (+8*3)(%rax)

	 mov	(+8*4)(%rax), %rsi
	 mov	(+8*5)(%rax), %rdi
	 mov	(+8*6)(%rax), %rcx
	 mov	(+8*7)(%rax), %rdx
	 cmovnc	%r12, %rsi
	 cmovnc	%r13, %rdi
	 cmovnc	%r14, %rcx
	 cmovnc	%r15, %rdx
	 mov	%rsi, (+8*4)(%rax)
	 mov	%rdi, (+8*5)(%rax)
	 mov	%rcx, (+8*6)(%rax)
	 mov	%rdx, (+8*7)(%rax)

	 mov	(+$rsp_offset)(%rsp), %rsi
	 mov	0(%rsi),%r15
	 mov	8(%rsi),%r14
	 mov	16(%rsi),%r13
	 mov	24(%rsi),%r12
	 mov	32(%rsi),%rbx
	 mov	40(%rsi),%rbp
	 lea	48(%rsi),%rsp
.Lepilogue:
	 ret
.size mod_exp_512, . - mod_exp_512
___

sub reg_part {
my ($reg,$conv)=@_;
    if ($reg =~ /%r[0-9]+/)	{ $reg .= $conv; }
    elsif ($conv eq "b")	{ $reg =~ s/%[er]([^x]+)x?/%$1l/;	}
    elsif ($conv eq "w")	{ $reg =~ s/%[er](.+)/%$1/;		}
    elsif ($conv eq "d")	{ $reg =~ s/%[er](.+)/%e$1/;		}
    return $reg;
}

$code =~ s/(%[a-z0-9]+)#([bwd])/reg_part($1,$2)/gem;
$code =~ s/\`([^\`]*)\`/eval $1/gem;
$code =~ s/(\(\+[^)]+\))/eval $1/gem;
print $code;
close STDOUT;
