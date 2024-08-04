// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains instructions for testing by the test titled:
 *
 *         "Test x86 instruction decoder - new instructions"
 *
 * Note that the 'Expecting' comment lines are consumed by the
 * gen-insn-x86-dat.awk script and have the format:
 *
 *         Expecting: <op> <branch> <rel>
 *
 * If this file is changed, remember to run the gen-insn-x86-dat.sh
 * script and commit the result.
 *
 * Refer to insn-x86.c for more details.
 */

int main(void)
{
	/* Following line is a marker for the awk script - do not change */
	asm volatile("rdtsc"); /* Start here */

	/* Test fix for vcvtph2ps in x86-opcode-map.txt */

	asm volatile("vcvtph2ps %xmm3,%ymm5");

#ifdef __x86_64__

	/* AVX-512: Instructions with the same op codes as Mask Instructions  */

	asm volatile("cmovno %rax,%rbx");
	asm volatile("cmovno 0x12345678(%rax),%rcx");
	asm volatile("cmovno 0x12345678(%rax),%cx");

	asm volatile("cmove  %rax,%rbx");
	asm volatile("cmove 0x12345678(%rax),%rcx");
	asm volatile("cmove 0x12345678(%rax),%cx");

	asm volatile("seto    0x12345678(%rax)");
	asm volatile("setno   0x12345678(%rax)");
	asm volatile("setb    0x12345678(%rax)");
	asm volatile("setc    0x12345678(%rax)");
	asm volatile("setnae  0x12345678(%rax)");
	asm volatile("setae   0x12345678(%rax)");
	asm volatile("setnb   0x12345678(%rax)");
	asm volatile("setnc   0x12345678(%rax)");
	asm volatile("sets    0x12345678(%rax)");
	asm volatile("setns   0x12345678(%rax)");

	/* AVX-512: Mask Instructions */

	asm volatile("kandw  %k7,%k6,%k5");
	asm volatile("kandq  %k7,%k6,%k5");
	asm volatile("kandb  %k7,%k6,%k5");
	asm volatile("kandd  %k7,%k6,%k5");

	asm volatile("kandnw  %k7,%k6,%k5");
	asm volatile("kandnq  %k7,%k6,%k5");
	asm volatile("kandnb  %k7,%k6,%k5");
	asm volatile("kandnd  %k7,%k6,%k5");

	asm volatile("knotw  %k7,%k6");
	asm volatile("knotq  %k7,%k6");
	asm volatile("knotb  %k7,%k6");
	asm volatile("knotd  %k7,%k6");

	asm volatile("korw  %k7,%k6,%k5");
	asm volatile("korq  %k7,%k6,%k5");
	asm volatile("korb  %k7,%k6,%k5");
	asm volatile("kord  %k7,%k6,%k5");

	asm volatile("kxnorw  %k7,%k6,%k5");
	asm volatile("kxnorq  %k7,%k6,%k5");
	asm volatile("kxnorb  %k7,%k6,%k5");
	asm volatile("kxnord  %k7,%k6,%k5");

	asm volatile("kxorw  %k7,%k6,%k5");
	asm volatile("kxorq  %k7,%k6,%k5");
	asm volatile("kxorb  %k7,%k6,%k5");
	asm volatile("kxord  %k7,%k6,%k5");

	asm volatile("kaddw  %k7,%k6,%k5");
	asm volatile("kaddq  %k7,%k6,%k5");
	asm volatile("kaddb  %k7,%k6,%k5");
	asm volatile("kaddd  %k7,%k6,%k5");

	asm volatile("kunpckbw %k7,%k6,%k5");
	asm volatile("kunpckwd %k7,%k6,%k5");
	asm volatile("kunpckdq %k7,%k6,%k5");

	asm volatile("kmovw  %k6,%k5");
	asm volatile("kmovw  (%rcx),%k5");
	asm volatile("kmovw  0x123(%rax,%r14,8),%k5");
	asm volatile("kmovw  %k5,(%rcx)");
	asm volatile("kmovw  %k5,0x123(%rax,%r14,8)");
	asm volatile("kmovw  %eax,%k5");
	asm volatile("kmovw  %ebp,%k5");
	asm volatile("kmovw  %r13d,%k5");
	asm volatile("kmovw  %k5,%eax");
	asm volatile("kmovw  %k5,%ebp");
	asm volatile("kmovw  %k5,%r13d");

	asm volatile("kmovq  %k6,%k5");
	asm volatile("kmovq  (%rcx),%k5");
	asm volatile("kmovq  0x123(%rax,%r14,8),%k5");
	asm volatile("kmovq  %k5,(%rcx)");
	asm volatile("kmovq  %k5,0x123(%rax,%r14,8)");
	asm volatile("kmovq  %rax,%k5");
	asm volatile("kmovq  %rbp,%k5");
	asm volatile("kmovq  %r13,%k5");
	asm volatile("kmovq  %k5,%rax");
	asm volatile("kmovq  %k5,%rbp");
	asm volatile("kmovq  %k5,%r13");

	asm volatile("kmovb  %k6,%k5");
	asm volatile("kmovb  (%rcx),%k5");
	asm volatile("kmovb  0x123(%rax,%r14,8),%k5");
	asm volatile("kmovb  %k5,(%rcx)");
	asm volatile("kmovb  %k5,0x123(%rax,%r14,8)");
	asm volatile("kmovb  %eax,%k5");
	asm volatile("kmovb  %ebp,%k5");
	asm volatile("kmovb  %r13d,%k5");
	asm volatile("kmovb  %k5,%eax");
	asm volatile("kmovb  %k5,%ebp");
	asm volatile("kmovb  %k5,%r13d");

	asm volatile("kmovd  %k6,%k5");
	asm volatile("kmovd  (%rcx),%k5");
	asm volatile("kmovd  0x123(%rax,%r14,8),%k5");
	asm volatile("kmovd  %k5,(%rcx)");
	asm volatile("kmovd  %k5,0x123(%rax,%r14,8)");
	asm volatile("kmovd  %eax,%k5");
	asm volatile("kmovd  %ebp,%k5");
	asm volatile("kmovd  %r13d,%k5");
	asm volatile("kmovd  %k5,%eax");
	asm volatile("kmovd  %k5,%ebp");
	asm volatile("kmovd %k5,%r13d");

	asm volatile("kortestw %k6,%k5");
	asm volatile("kortestq %k6,%k5");
	asm volatile("kortestb %k6,%k5");
	asm volatile("kortestd %k6,%k5");

	asm volatile("ktestw %k6,%k5");
	asm volatile("ktestq %k6,%k5");
	asm volatile("ktestb %k6,%k5");
	asm volatile("ktestd %k6,%k5");

	asm volatile("kshiftrw $0x12,%k6,%k5");
	asm volatile("kshiftrq $0x5b,%k6,%k5");
	asm volatile("kshiftlw $0x12,%k6,%k5");
	asm volatile("kshiftlq $0x5b,%k6,%k5");

	/* AVX-512: Op code 0f 5b */
	asm volatile("vcvtdq2ps %xmm5,%xmm6");
	asm volatile("vcvtqq2ps %zmm29,%ymm6{%k7}");
	asm volatile("vcvtps2dq %xmm5,%xmm6");
	asm volatile("vcvttps2dq %xmm5,%xmm6");

	/* AVX-512: Op code 0f 6f */

	asm volatile("movq   %mm0,%mm4");
	asm volatile("vmovdqa %ymm4,%ymm6");
	asm volatile("vmovdqa32 %zmm25,%zmm26");
	asm volatile("vmovdqa64 %zmm25,%zmm26");
	asm volatile("vmovdqu %ymm4,%ymm6");
	asm volatile("vmovdqu32 %zmm29,%zmm30");
	asm volatile("vmovdqu64 %zmm25,%zmm26");
	asm volatile("vmovdqu8 %zmm29,%zmm30");
	asm volatile("vmovdqu16 %zmm25,%zmm26");

	/* AVX-512: Op code 0f 78 */

	asm volatile("vmread %rax,%rbx");
	asm volatile("vcvttps2udq %zmm25,%zmm26");
	asm volatile("vcvttpd2udq %zmm29,%ymm6{%k7}");
	asm volatile("vcvttsd2usi %xmm6,%rax");
	asm volatile("vcvttss2usi %xmm6,%rax");
	asm volatile("vcvttps2uqq %ymm5,%zmm26{%k7}");
	asm volatile("vcvttpd2uqq %zmm29,%zmm30");

	/* AVX-512: Op code 0f 79 */

	asm volatile("vmwrite %rax,%rbx");
	asm volatile("vcvtps2udq %zmm25,%zmm26");
	asm volatile("vcvtpd2udq %zmm29,%ymm6{%k7}");
	asm volatile("vcvtsd2usi %xmm6,%rax");
	asm volatile("vcvtss2usi %xmm6,%rax");
	asm volatile("vcvtps2uqq %ymm5,%zmm26{%k7}");
	asm volatile("vcvtpd2uqq %zmm29,%zmm30");

	/* AVX-512: Op code 0f 7a */

	asm volatile("vcvtudq2pd %ymm5,%zmm29{%k7}");
	asm volatile("vcvtuqq2pd %zmm25,%zmm26");
	asm volatile("vcvtudq2ps %zmm29,%zmm30");
	asm volatile("vcvtuqq2ps %zmm25,%ymm26{%k7}");
	asm volatile("vcvttps2qq %ymm25,%zmm26{%k7}");
	asm volatile("vcvttpd2qq %zmm29,%zmm30");

	/* AVX-512: Op code 0f 7b */

	asm volatile("vcvtusi2sd %eax,%xmm5,%xmm6");
	asm volatile("vcvtusi2ss %eax,%xmm5,%xmm6");
	asm volatile("vcvtps2qq %ymm5,%zmm26{%k7}");
	asm volatile("vcvtpd2qq %zmm29,%zmm30");

	/* AVX-512: Op code 0f 7f */

	asm volatile("movq.s  %mm0,%mm4");
	asm volatile("vmovdqa %ymm8,%ymm6");
	asm volatile("vmovdqa32.s %zmm25,%zmm26");
	asm volatile("vmovdqa64.s %zmm25,%zmm26");
	asm volatile("vmovdqu %ymm8,%ymm6");
	asm volatile("vmovdqu32.s %zmm25,%zmm26");
	asm volatile("vmovdqu64.s %zmm25,%zmm26");
	asm volatile("vmovdqu8.s %zmm30,(%rcx)");
	asm volatile("vmovdqu16.s %zmm25,%zmm26");

	/* AVX-512: Op code 0f db */

	asm volatile("pand  %mm1,%mm2");
	asm volatile("pand  %xmm1,%xmm2");
	asm volatile("vpand  %ymm4,%ymm6,%ymm2");
	asm volatile("vpandd %zmm24,%zmm25,%zmm26");
	asm volatile("vpandq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f df */

	asm volatile("pandn  %mm1,%mm2");
	asm volatile("pandn  %xmm1,%xmm2");
	asm volatile("vpandn %ymm4,%ymm6,%ymm2");
	asm volatile("vpandnd %zmm24,%zmm25,%zmm26");
	asm volatile("vpandnq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f e6 */

	asm volatile("vcvttpd2dq %xmm1,%xmm2");
	asm volatile("vcvtdq2pd %xmm5,%xmm6");
	asm volatile("vcvtdq2pd %ymm5,%zmm26{%k7}");
	asm volatile("vcvtqq2pd %zmm25,%zmm26");
	asm volatile("vcvtpd2dq %xmm1,%xmm2");

	/* AVX-512: Op code 0f eb */

	asm volatile("por   %mm4,%mm6");
	asm volatile("vpor   %ymm4,%ymm6,%ymm2");
	asm volatile("vpord  %zmm24,%zmm25,%zmm26");
	asm volatile("vporq  %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f ef */

	asm volatile("pxor   %mm4,%mm6");
	asm volatile("vpxor  %ymm4,%ymm6,%ymm2");
	asm volatile("vpxord %zmm24,%zmm25,%zmm26");
	asm volatile("vpxorq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 10 */

	asm volatile("pblendvb %xmm1,%xmm0");
	asm volatile("vpsrlvw %zmm27,%zmm28,%zmm29");
	asm volatile("vpmovuswb %zmm28,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 11 */

	asm volatile("vpmovusdb %zmm28,%xmm6{%k7}");
	asm volatile("vpsravw %zmm27,%zmm28,%zmm29");

	/* AVX-512: Op code 0f 38 12 */

	asm volatile("vpmovusqb %zmm27,%xmm6{%k7}");
	asm volatile("vpsllvw %zmm27,%zmm28,%zmm29");

	/* AVX-512: Op code 0f 38 13 */

	asm volatile("vcvtph2ps %xmm3,%ymm5");
	asm volatile("vcvtph2ps %ymm5,%zmm27{%k7}");
	asm volatile("vpmovusdw %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 14 */

	asm volatile("blendvps %xmm1,%xmm0");
	asm volatile("vpmovusqw %zmm27,%xmm6{%k7}");
	asm volatile("vprorvd %zmm27,%zmm28,%zmm29");
	asm volatile("vprorvq %zmm27,%zmm28,%zmm29");

	/* AVX-512: Op code 0f 38 15 */

	asm volatile("blendvpd %xmm1,%xmm0");
	asm volatile("vpmovusqd %zmm27,%ymm6{%k7}");
	asm volatile("vprolvd %zmm27,%zmm28,%zmm29");
	asm volatile("vprolvq %zmm27,%zmm28,%zmm29");

	/* AVX-512: Op code 0f 38 16 */

	asm volatile("vpermps %ymm4,%ymm6,%ymm2");
	asm volatile("vpermps %ymm24,%ymm26,%ymm22{%k7}");
	asm volatile("vpermpd %ymm24,%ymm26,%ymm22{%k7}");

	/* AVX-512: Op code 0f 38 19 */

	asm volatile("vbroadcastsd %xmm4,%ymm6");
	asm volatile("vbroadcastf32x2 %xmm27,%zmm26");

	/* AVX-512: Op code 0f 38 1a */

	asm volatile("vbroadcastf128 (%rcx),%ymm4");
	asm volatile("vbroadcastf32x4 (%rcx),%zmm26");
	asm volatile("vbroadcastf64x2 (%rcx),%zmm26");

	/* AVX-512: Op code 0f 38 1b */

	asm volatile("vbroadcastf32x8 (%rcx),%zmm27");
	asm volatile("vbroadcastf64x4 (%rcx),%zmm26");

	/* AVX-512: Op code 0f 38 1f */

	asm volatile("vpabsq %zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 20 */

	asm volatile("vpmovsxbw %xmm4,%xmm5");
	asm volatile("vpmovswb %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 21 */

	asm volatile("vpmovsxbd %xmm4,%ymm6");
	asm volatile("vpmovsdb %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 22 */

	asm volatile("vpmovsxbq %xmm4,%ymm4");
	asm volatile("vpmovsqb %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 23 */

	asm volatile("vpmovsxwd %xmm4,%ymm4");
	asm volatile("vpmovsdw %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 24 */

	asm volatile("vpmovsxwq %xmm4,%ymm6");
	asm volatile("vpmovsqw %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 25 */

	asm volatile("vpmovsxdq %xmm4,%ymm4");
	asm volatile("vpmovsqd %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 26 */

	asm volatile("vptestmb %zmm27,%zmm28,%k5");
	asm volatile("vptestmw %zmm27,%zmm28,%k5");
	asm volatile("vptestnmb %zmm26,%zmm27,%k5");
	asm volatile("vptestnmw %zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 38 27 */

	asm volatile("vptestmd %zmm27,%zmm28,%k5");
	asm volatile("vptestmq %zmm27,%zmm28,%k5");
	asm volatile("vptestnmd %zmm26,%zmm27,%k5");
	asm volatile("vptestnmq %zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 38 28 */

	asm volatile("vpmuldq %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovm2b %k5,%zmm28");
	asm volatile("vpmovm2w %k5,%zmm28");

	/* AVX-512: Op code 0f 38 29 */

	asm volatile("vpcmpeqq %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovb2m %zmm28,%k5");
	asm volatile("vpmovw2m %zmm28,%k5");

	/* AVX-512: Op code 0f 38 2a */

	asm volatile("vmovntdqa (%rcx),%ymm4");
	asm volatile("vpbroadcastmb2q %k6,%zmm30");

	/* AVX-512: Op code 0f 38 2c */

	asm volatile("vmaskmovps (%rcx),%ymm4,%ymm6");
	asm volatile("vscalefps %zmm24,%zmm25,%zmm26");
	asm volatile("vscalefpd %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 2d */

	asm volatile("vmaskmovpd (%rcx),%ymm4,%ymm6");
	asm volatile("vscalefss %xmm24,%xmm25,%xmm26{%k7}");
	asm volatile("vscalefsd %xmm24,%xmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 38 30 */

	asm volatile("vpmovzxbw %xmm4,%ymm4");
	asm volatile("vpmovwb %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 31 */

	asm volatile("vpmovzxbd %xmm4,%ymm6");
	asm volatile("vpmovdb %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 32 */

	asm volatile("vpmovzxbq %xmm4,%ymm4");
	asm volatile("vpmovqb %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 33 */

	asm volatile("vpmovzxwd %xmm4,%ymm4");
	asm volatile("vpmovdw %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 34 */

	asm volatile("vpmovzxwq %xmm4,%ymm6");
	asm volatile("vpmovqw %zmm27,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 35 */

	asm volatile("vpmovzxdq %xmm4,%ymm4");
	asm volatile("vpmovqd %zmm27,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 38 */

	asm volatile("vpermd %ymm4,%ymm6,%ymm2");
	asm volatile("vpermd %ymm24,%ymm26,%ymm22{%k7}");
	asm volatile("vpermq %ymm24,%ymm26,%ymm22{%k7}");

	/* AVX-512: Op code 0f 38 38 */

	asm volatile("vpminsb %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovm2d %k5,%zmm28");
	asm volatile("vpmovm2q %k5,%zmm28");

	/* AVX-512: Op code 0f 38 39 */

	asm volatile("vpminsd %xmm1,%xmm2,%xmm3");
	asm volatile("vpminsd %zmm24,%zmm25,%zmm26");
	asm volatile("vpminsq %zmm24,%zmm25,%zmm26");
	asm volatile("vpmovd2m %zmm28,%k5");
	asm volatile("vpmovq2m %zmm28,%k5");

	/* AVX-512: Op code 0f 38 3a */

	asm volatile("vpminuw %ymm4,%ymm6,%ymm2");
	asm volatile("vpbroadcastmw2d %k6,%zmm28");

	/* AVX-512: Op code 0f 38 3b */

	asm volatile("vpminud %ymm4,%ymm6,%ymm2");
	asm volatile("vpminud %zmm24,%zmm25,%zmm26");
	asm volatile("vpminuq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 3d */

	asm volatile("vpmaxsd %ymm4,%ymm6,%ymm2");
	asm volatile("vpmaxsd %zmm24,%zmm25,%zmm26");
	asm volatile("vpmaxsq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 3f */

	asm volatile("vpmaxud %ymm4,%ymm6,%ymm2");
	asm volatile("vpmaxud %zmm24,%zmm25,%zmm26");
	asm volatile("vpmaxuq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 42 */

	asm volatile("vpmulld %ymm4,%ymm6,%ymm2");
	asm volatile("vpmulld %zmm24,%zmm25,%zmm26");
	asm volatile("vpmullq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 42 */

	asm volatile("vgetexpps %zmm25,%zmm26");
	asm volatile("vgetexppd %zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 43 */

	asm volatile("vgetexpss %xmm24,%xmm25,%xmm26{%k7}");
	asm volatile("vgetexpsd %xmm28,%xmm29,%xmm30{%k7}");

	/* AVX-512: Op code 0f 38 44 */

	asm volatile("vplzcntd %zmm27,%zmm28");
	asm volatile("vplzcntq %zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 46 */

	asm volatile("vpsravd %ymm4,%ymm6,%ymm2");
	asm volatile("vpsravd %zmm24,%zmm25,%zmm26");
	asm volatile("vpsravq %zmm24,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 38 4c */

	asm volatile("vrcp14ps %zmm25,%zmm26");
	asm volatile("vrcp14pd %zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 4d */

	asm volatile("vrcp14ss %xmm24,%xmm25,%xmm26{%k7}");
	asm volatile("vrcp14sd %xmm24,%xmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 38 4e */

	asm volatile("vrsqrt14ps %zmm25,%zmm26");
	asm volatile("vrsqrt14pd %zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 4f */

	asm volatile("vrsqrt14ss %xmm24,%xmm25,%xmm26{%k7}");
	asm volatile("vrsqrt14sd %xmm24,%xmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 38 50 */

	asm volatile("vpdpbusd %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpbusd %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpbusd %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpbusd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpdpbusd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 51 */

	asm volatile("vpdpbusds %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpbusds %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpbusds %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpbusds 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpdpbusds 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 52 */

	asm volatile("vdpbf16ps %xmm1, %xmm2, %xmm3");
	asm volatile("vdpbf16ps %ymm1, %ymm2, %ymm3");
	asm volatile("vdpbf16ps %zmm1, %zmm2, %zmm3");
	asm volatile("vdpbf16ps 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vdpbf16ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpdpwssd %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpwssd %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpwssd %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpwssd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpdpwssd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vp4dpwssd (%rax), %zmm0, %zmm4");
	asm volatile("vp4dpwssd (%eax), %zmm0, %zmm4");
	asm volatile("vp4dpwssd 0x12345678(%rax,%rcx,8),%zmm0,%zmm4");
	asm volatile("vp4dpwssd 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 53 */

	asm volatile("vpdpwssds %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpwssds %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpwssds %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpwssds 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpdpwssds 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vp4dpwssds (%rax), %zmm0, %zmm4");
	asm volatile("vp4dpwssds (%eax), %zmm0, %zmm4");
	asm volatile("vp4dpwssds 0x12345678(%rax,%rcx,8),%zmm0,%zmm4");
	asm volatile("vp4dpwssds 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 54 */

	asm volatile("vpopcntb %xmm1, %xmm2");
	asm volatile("vpopcntb %ymm1, %ymm2");
	asm volatile("vpopcntb %zmm1, %zmm2");
	asm volatile("vpopcntb 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpopcntb 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpopcntw %xmm1, %xmm2");
	asm volatile("vpopcntw %ymm1, %ymm2");
	asm volatile("vpopcntw %zmm1, %zmm2");
	asm volatile("vpopcntw 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpopcntw 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 55 */

	asm volatile("vpopcntd %xmm1, %xmm2");
	asm volatile("vpopcntd %ymm1, %ymm2");
	asm volatile("vpopcntd %zmm1, %zmm2");
	asm volatile("vpopcntd 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpopcntd 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpopcntq %xmm1, %xmm2");
	asm volatile("vpopcntq %ymm1, %ymm2");
	asm volatile("vpopcntq %zmm1, %zmm2");
	asm volatile("vpopcntq 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpopcntq 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 59 */

	asm volatile("vpbroadcastq %xmm4,%xmm6");
	asm volatile("vbroadcasti32x2 %xmm27,%zmm26");

	/* AVX-512: Op code 0f 38 5a */

	asm volatile("vbroadcasti128 (%rcx),%ymm4");
	asm volatile("vbroadcasti32x4 (%rcx),%zmm26");
	asm volatile("vbroadcasti64x2 (%rcx),%zmm26");

	/* AVX-512: Op code 0f 38 5b */

	asm volatile("vbroadcasti32x8 (%rcx),%zmm28");
	asm volatile("vbroadcasti64x4 (%rcx),%zmm26");

	/* AVX-512: Op code 0f 38 62 */

	asm volatile("vpexpandb %xmm1, %xmm2");
	asm volatile("vpexpandb %ymm1, %ymm2");
	asm volatile("vpexpandb %zmm1, %zmm2");
	asm volatile("vpexpandb 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpexpandb 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpexpandw %xmm1, %xmm2");
	asm volatile("vpexpandw %ymm1, %ymm2");
	asm volatile("vpexpandw %zmm1, %zmm2");
	asm volatile("vpexpandw 0x12345678(%rax,%rcx,8),%zmm2");
	asm volatile("vpexpandw 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 63 */

	asm volatile("vpcompressb %xmm1, %xmm2");
	asm volatile("vpcompressb %ymm1, %ymm2");
	asm volatile("vpcompressb %zmm1, %zmm2");
	asm volatile("vpcompressb %zmm2,0x12345678(%rax,%rcx,8)");
	asm volatile("vpcompressb %zmm2,0x12345678(%eax,%ecx,8)");

	asm volatile("vpcompressw %xmm1, %xmm2");
	asm volatile("vpcompressw %ymm1, %ymm2");
	asm volatile("vpcompressw %zmm1, %zmm2");
	asm volatile("vpcompressw %zmm2,0x12345678(%rax,%rcx,8)");
	asm volatile("vpcompressw %zmm2,0x12345678(%eax,%ecx,8)");

	/* AVX-512: Op code 0f 38 64 */

	asm volatile("vpblendmd %zmm26,%zmm27,%zmm28");
	asm volatile("vpblendmq %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 65 */

	asm volatile("vblendmps %zmm24,%zmm25,%zmm26");
	asm volatile("vblendmpd %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 66 */

	asm volatile("vpblendmb %zmm26,%zmm27,%zmm28");
	asm volatile("vpblendmw %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 68 */

	asm volatile("vp2intersectd %xmm1, %xmm2, %k3");
	asm volatile("vp2intersectd %ymm1, %ymm2, %k3");
	asm volatile("vp2intersectd %zmm1, %zmm2, %k3");
	asm volatile("vp2intersectd 0x12345678(%rax,%rcx,8),%zmm2,%k3");
	asm volatile("vp2intersectd 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	asm volatile("vp2intersectq %xmm1, %xmm2, %k3");
	asm volatile("vp2intersectq %ymm1, %ymm2, %k3");
	asm volatile("vp2intersectq %zmm1, %zmm2, %k3");
	asm volatile("vp2intersectq 0x12345678(%rax,%rcx,8),%zmm2,%k3");
	asm volatile("vp2intersectq 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	/* AVX-512: Op code 0f 38 70 */

	asm volatile("vpshldvw %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvw %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvw %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvw 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshldvw 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 71 */

	asm volatile("vpshldvd %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvd %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvd %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshldvd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpshldvq %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvq %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvq %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvq 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshldvq 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 72 */

	asm volatile("vcvtne2ps2bf16 %xmm1, %xmm2, %xmm3");
	asm volatile("vcvtne2ps2bf16 %ymm1, %ymm2, %ymm3");
	asm volatile("vcvtne2ps2bf16 %zmm1, %zmm2, %zmm3");
	asm volatile("vcvtne2ps2bf16 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vcvtne2ps2bf16 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vcvtneps2bf16 %xmm1, %xmm2");
	asm volatile("vcvtneps2bf16 %ymm1, %xmm2");
	asm volatile("vcvtneps2bf16 %zmm1, %ymm2");
	asm volatile("vcvtneps2bf16 0x12345678(%rax,%rcx,8),%ymm2");
	asm volatile("vcvtneps2bf16 0x12345678(%eax,%ecx,8),%ymm2");

	asm volatile("vpshrdvw %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvw %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvw %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvw 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshrdvw 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 73 */

	asm volatile("vpshrdvd %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvd %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvd %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshrdvd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpshrdvq %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvq %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvq %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvq 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vpshrdvq 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 75 */

	asm volatile("vpermi2b %zmm24,%zmm25,%zmm26");
	asm volatile("vpermi2w %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 76 */

	asm volatile("vpermi2d %zmm26,%zmm27,%zmm28");
	asm volatile("vpermi2q %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 77 */

	asm volatile("vpermi2ps %zmm26,%zmm27,%zmm28");
	asm volatile("vpermi2pd %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 7a */

	asm volatile("vpbroadcastb %eax,%xmm30");

	/* AVX-512: Op code 0f 38 7b */

	asm volatile("vpbroadcastw %eax,%xmm30");

	/* AVX-512: Op code 0f 38 7c */

	asm volatile("vpbroadcastd %eax,%xmm30");
	asm volatile("vpbroadcastq %rax,%zmm30");

	/* AVX-512: Op code 0f 38 7d */

	asm volatile("vpermt2b %zmm26,%zmm27,%zmm28");
	asm volatile("vpermt2w %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 7e */

	asm volatile("vpermt2d %zmm26,%zmm27,%zmm28");
	asm volatile("vpermt2q %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 7f */

	asm volatile("vpermt2ps %zmm26,%zmm27,%zmm28");
	asm volatile("vpermt2pd %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 83 */

	asm volatile("vpmultishiftqb %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 88 */

	asm volatile("vexpandps (%rcx),%zmm26");
	asm volatile("vexpandpd (%rcx),%zmm28");

	/* AVX-512: Op code 0f 38 89 */

	asm volatile("vpexpandd (%rcx),%zmm28");
	asm volatile("vpexpandq (%rcx),%zmm26");

	/* AVX-512: Op code 0f 38 8a */

	asm volatile("vcompressps %zmm28,(%rcx)");
	asm volatile("vcompresspd %zmm28,(%rcx)");

	/* AVX-512: Op code 0f 38 8b */

	asm volatile("vpcompressd %zmm28,(%rcx)");
	asm volatile("vpcompressq %zmm26,(%rcx)");

	/* AVX-512: Op code 0f 38 8d */

	asm volatile("vpermb %zmm26,%zmm27,%zmm28");
	asm volatile("vpermw %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 8f */

	asm volatile("vpshufbitqmb %xmm1, %xmm2, %k3");
	asm volatile("vpshufbitqmb %ymm1, %ymm2, %k3");
	asm volatile("vpshufbitqmb %zmm1, %zmm2, %k3");
	asm volatile("vpshufbitqmb 0x12345678(%rax,%rcx,8),%zmm2,%k3");
	asm volatile("vpshufbitqmb 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	/* AVX-512: Op code 0f 38 90 */

	asm volatile("vpgatherdd %xmm2,0x02(%rbp,%xmm7,2),%xmm1");
	asm volatile("vpgatherdq %xmm2,0x04(%rbp,%xmm7,2),%xmm1");
	asm volatile("vpgatherdd 0x7b(%rbp,%zmm27,8),%zmm26{%k1}");
	asm volatile("vpgatherdq 0x7b(%rbp,%ymm27,8),%zmm26{%k1}");

	/* AVX-512: Op code 0f 38 91 */

	asm volatile("vpgatherqd %xmm2,0x02(%rbp,%xmm7,2),%xmm1");
	asm volatile("vpgatherqq %xmm2,0x02(%rbp,%xmm7,2),%xmm1");
	asm volatile("vpgatherqd 0x7b(%rbp,%zmm27,8),%ymm26{%k1}");
	asm volatile("vpgatherqq 0x7b(%rbp,%zmm27,8),%zmm26{%k1}");

	/* AVX-512: Op code 0f 38 9a */

	asm volatile("vfmsub132ps %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132ps %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub132ps %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub132ps 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vfmsub132ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vfmsub132pd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132pd %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub132pd %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub132pd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vfmsub132pd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("v4fmaddps (%rax), %zmm0, %zmm4");
	asm volatile("v4fmaddps (%eax), %zmm0, %zmm4");
	asm volatile("v4fmaddps 0x12345678(%rax,%rcx,8),%zmm0,%zmm4");
	asm volatile("v4fmaddps 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 9b */

	asm volatile("vfmsub132ss %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132ss 0x12345678(%rax,%rcx,8),%xmm2,%xmm3");
	asm volatile("vfmsub132ss 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("vfmsub132sd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132sd 0x12345678(%rax,%rcx,8),%xmm2,%xmm3");
	asm volatile("vfmsub132sd 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("v4fmaddss (%rax), %xmm0, %xmm4");
	asm volatile("v4fmaddss (%eax), %xmm0, %xmm4");
	asm volatile("v4fmaddss 0x12345678(%rax,%rcx,8),%xmm0,%xmm4");
	asm volatile("v4fmaddss 0x12345678(%eax,%ecx,8),%xmm0,%xmm4");

	/* AVX-512: Op code 0f 38 a0 */

	asm volatile("vpscatterdd %zmm28,0x7b(%rbp,%zmm29,8){%k1}");
	asm volatile("vpscatterdq %zmm26,0x7b(%rbp,%ymm27,8){%k1}");

	/* AVX-512: Op code 0f 38 a1 */

	asm volatile("vpscatterqd %ymm6,0x7b(%rbp,%zmm29,8){%k1}");
	asm volatile("vpscatterqq %ymm6,0x7b(%rbp,%ymm27,8){%k1}");

	/* AVX-512: Op code 0f 38 a2 */

	asm volatile("vscatterdps %zmm28,0x7b(%rbp,%zmm29,8){%k1}");
	asm volatile("vscatterdpd %zmm28,0x7b(%rbp,%ymm27,8){%k1}");

	/* AVX-512: Op code 0f 38 a3 */

	asm volatile("vscatterqps %ymm6,0x7b(%rbp,%zmm29,8){%k1}");
	asm volatile("vscatterqpd %zmm28,0x7b(%rbp,%zmm29,8){%k1}");

	/* AVX-512: Op code 0f 38 aa */

	asm volatile("vfmsub213ps %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213ps %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub213ps %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub213ps 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vfmsub213ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vfmsub213pd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213pd %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub213pd %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub213pd 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vfmsub213pd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("v4fnmaddps (%rax), %zmm0, %zmm4");
	asm volatile("v4fnmaddps (%eax), %zmm0, %zmm4");
	asm volatile("v4fnmaddps 0x12345678(%rax,%rcx,8),%zmm0,%zmm4");
	asm volatile("v4fnmaddps 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 ab */

	asm volatile("vfmsub213ss %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213ss 0x12345678(%rax,%rcx,8),%xmm2,%xmm3");
	asm volatile("vfmsub213ss 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("vfmsub213sd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213sd 0x12345678(%rax,%rcx,8),%xmm2,%xmm3");
	asm volatile("vfmsub213sd 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("v4fnmaddss (%rax), %xmm0, %xmm4");
	asm volatile("v4fnmaddss (%eax), %xmm0, %xmm4");
	asm volatile("v4fnmaddss 0x12345678(%rax,%rcx,8),%xmm0,%xmm4");
	asm volatile("v4fnmaddss 0x12345678(%eax,%ecx,8),%xmm0,%xmm4");

	/* AVX-512: Op code 0f 38 b4 */

	asm volatile("vpmadd52luq %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 b5 */

	asm volatile("vpmadd52huq %zmm26,%zmm27,%zmm28");

	/* AVX-512: Op code 0f 38 c4 */

	asm volatile("vpconflictd %zmm26,%zmm27");
	asm volatile("vpconflictq %zmm26,%zmm27");

	/* AVX-512: Op code 0f 38 c8 */

	asm volatile("vexp2ps %zmm29,%zmm30");
	asm volatile("vexp2pd %zmm26,%zmm27");

	/* AVX-512: Op code 0f 38 ca */

	asm volatile("vrcp28ps %zmm29,%zmm30");
	asm volatile("vrcp28pd %zmm26,%zmm27");

	/* AVX-512: Op code 0f 38 cb */

	asm volatile("vrcp28ss %xmm28,%xmm29,%xmm30{%k7}");
	asm volatile("vrcp28sd %xmm25,%xmm26,%xmm27{%k7}");

	/* AVX-512: Op code 0f 38 cc */

	asm volatile("vrsqrt28ps %zmm29,%zmm30");
	asm volatile("vrsqrt28pd %zmm26,%zmm27");

	/* AVX-512: Op code 0f 38 cd */

	asm volatile("vrsqrt28ss %xmm28,%xmm29,%xmm30{%k7}");
	asm volatile("vrsqrt28sd %xmm25,%xmm26,%xmm27{%k7}");

	/* AVX-512: Op code 0f 38 cf */

	asm volatile("gf2p8mulb %xmm1, %xmm3");
	asm volatile("gf2p8mulb 0x12345678(%rax,%rcx,8),%xmm3");
	asm volatile("gf2p8mulb 0x12345678(%eax,%ecx,8),%xmm3");

	asm volatile("vgf2p8mulb %xmm1, %xmm2, %xmm3");
	asm volatile("vgf2p8mulb %ymm1, %ymm2, %ymm3");
	asm volatile("vgf2p8mulb %zmm1, %zmm2, %zmm3");
	asm volatile("vgf2p8mulb 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vgf2p8mulb 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 dc */

	asm volatile("vaesenc %xmm1, %xmm2, %xmm3");
	asm volatile("vaesenc %ymm1, %ymm2, %ymm3");
	asm volatile("vaesenc %zmm1, %zmm2, %zmm3");
	asm volatile("vaesenc 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vaesenc 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 dd */

	asm volatile("vaesenclast %xmm1, %xmm2, %xmm3");
	asm volatile("vaesenclast %ymm1, %ymm2, %ymm3");
	asm volatile("vaesenclast %zmm1, %zmm2, %zmm3");
	asm volatile("vaesenclast 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vaesenclast 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 de */

	asm volatile("vaesdec %xmm1, %xmm2, %xmm3");
	asm volatile("vaesdec %ymm1, %ymm2, %ymm3");
	asm volatile("vaesdec %zmm1, %zmm2, %zmm3");
	asm volatile("vaesdec 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vaesdec 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 df */

	asm volatile("vaesdeclast %xmm1, %xmm2, %xmm3");
	asm volatile("vaesdeclast %ymm1, %ymm2, %ymm3");
	asm volatile("vaesdeclast %zmm1, %zmm2, %zmm3");
	asm volatile("vaesdeclast 0x12345678(%rax,%rcx,8),%zmm2,%zmm3");
	asm volatile("vaesdeclast 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 03 */

	asm volatile("valignd $0x12,%zmm28,%zmm29,%zmm30");
	asm volatile("valignq $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 08 */

	asm volatile("vroundps $0x5,%ymm6,%ymm2");
	asm volatile("vrndscaleps $0x12,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 3a 09 */

	asm volatile("vroundpd $0x5,%ymm6,%ymm2");
	asm volatile("vrndscalepd $0x12,%zmm25,%zmm26");

	/* AVX-512: Op code 0f 3a 1a */

	asm volatile("vroundss $0x5,%xmm4,%xmm6,%xmm2");
	asm volatile("vrndscaless $0x12,%xmm24,%xmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 3a 0b */

	asm volatile("vroundsd $0x5,%xmm4,%xmm6,%xmm2");
	asm volatile("vrndscalesd $0x12,%xmm24,%xmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 3a 18 */

	asm volatile("vinsertf128 $0x5,%xmm4,%ymm4,%ymm6");
	asm volatile("vinsertf32x4 $0x12,%xmm24,%zmm25,%zmm26{%k7}");
	asm volatile("vinsertf64x2 $0x12,%xmm24,%zmm25,%zmm26{%k7}");

	/* AVX-512: Op code 0f 3a 19 */

	asm volatile("vextractf128 $0x5,%ymm4,%xmm4");
	asm volatile("vextractf32x4 $0x12,%zmm25,%xmm26{%k7}");
	asm volatile("vextractf64x2 $0x12,%zmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 3a 1a */

	asm volatile("vinsertf32x8 $0x12,%ymm25,%zmm26,%zmm27{%k7}");
	asm volatile("vinsertf64x4 $0x12,%ymm28,%zmm29,%zmm30{%k7}");

	/* AVX-512: Op code 0f 3a 1b */

	asm volatile("vextractf32x8 $0x12,%zmm29,%ymm30{%k7}");
	asm volatile("vextractf64x4 $0x12,%zmm26,%ymm27{%k7}");

	/* AVX-512: Op code 0f 3a 1e */

	asm volatile("vpcmpud $0x12,%zmm29,%zmm30,%k5");
	asm volatile("vpcmpuq $0x12,%zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 3a 1f */

	asm volatile("vpcmpd $0x12,%zmm29,%zmm30,%k5");
	asm volatile("vpcmpq $0x12,%zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 3a 23 */

	asm volatile("vshuff32x4 $0x12,%zmm28,%zmm29,%zmm30");
	asm volatile("vshuff64x2 $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 25 */

	asm volatile("vpternlogd $0x12,%zmm28,%zmm29,%zmm30");
	asm volatile("vpternlogq $0x12,%zmm28,%zmm29,%zmm30");

	/* AVX-512: Op code 0f 3a 26 */

	asm volatile("vgetmantps $0x12,%zmm26,%zmm27");
	asm volatile("vgetmantpd $0x12,%zmm29,%zmm30");

	/* AVX-512: Op code 0f 3a 27 */

	asm volatile("vgetmantss $0x12,%xmm25,%xmm26,%xmm27{%k7}");
	asm volatile("vgetmantsd $0x12,%xmm28,%xmm29,%xmm30{%k7}");

	/* AVX-512: Op code 0f 3a 38 */

	asm volatile("vinserti128 $0x5,%xmm4,%ymm4,%ymm6");
	asm volatile("vinserti32x4 $0x12,%xmm24,%zmm25,%zmm26{%k7}");
	asm volatile("vinserti64x2 $0x12,%xmm24,%zmm25,%zmm26{%k7}");

	/* AVX-512: Op code 0f 3a 39 */

	asm volatile("vextracti128 $0x5,%ymm4,%xmm6");
	asm volatile("vextracti32x4 $0x12,%zmm25,%xmm26{%k7}");
	asm volatile("vextracti64x2 $0x12,%zmm25,%xmm26{%k7}");

	/* AVX-512: Op code 0f 3a 3a */

	asm volatile("vinserti32x8 $0x12,%ymm28,%zmm29,%zmm30{%k7}");
	asm volatile("vinserti64x4 $0x12,%ymm25,%zmm26,%zmm27{%k7}");

	/* AVX-512: Op code 0f 3a 3b */

	asm volatile("vextracti32x8 $0x12,%zmm29,%ymm30{%k7}");
	asm volatile("vextracti64x4 $0x12,%zmm26,%ymm27{%k7}");

	/* AVX-512: Op code 0f 3a 3e */

	asm volatile("vpcmpub $0x12,%zmm29,%zmm30,%k5");
	asm volatile("vpcmpuw $0x12,%zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 3a 3f */

	asm volatile("vpcmpb $0x12,%zmm29,%zmm30,%k5");
	asm volatile("vpcmpw $0x12,%zmm26,%zmm27,%k5");

	/* AVX-512: Op code 0f 3a 43 */

	asm volatile("vmpsadbw $0x5,%ymm4,%ymm6,%ymm2");
	asm volatile("vdbpsadbw $0x12,%zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 3a 43 */

	asm volatile("vshufi32x4 $0x12,%zmm25,%zmm26,%zmm27");
	asm volatile("vshufi64x2 $0x12,%zmm28,%zmm29,%zmm30");

	/* AVX-512: Op code 0f 3a 44 */

	asm volatile("vpclmulqdq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpclmulqdq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpclmulqdq $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpclmulqdq $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 50 */

	asm volatile("vrangeps $0x12,%zmm25,%zmm26,%zmm27");
	asm volatile("vrangepd $0x12,%zmm28,%zmm29,%zmm30");

	/* AVX-512: Op code 0f 3a 51 */

	asm volatile("vrangess $0x12,%xmm25,%xmm26,%xmm27");
	asm volatile("vrangesd $0x12,%xmm28,%xmm29,%xmm30");

	/* AVX-512: Op code 0f 3a 54 */

	asm volatile("vfixupimmps $0x12,%zmm28,%zmm29,%zmm30");
	asm volatile("vfixupimmpd $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 55 */

	asm volatile("vfixupimmss $0x12,%xmm28,%xmm29,%xmm30{%k7}");
	asm volatile("vfixupimmsd $0x12,%xmm25,%xmm26,%xmm27{%k7}");

	/* AVX-512: Op code 0f 3a 56 */

	asm volatile("vreduceps $0x12,%zmm26,%zmm27");
	asm volatile("vreducepd $0x12,%zmm29,%zmm30");

	/* AVX-512: Op code 0f 3a 57 */

	asm volatile("vreducess $0x12,%xmm25,%xmm26,%xmm27");
	asm volatile("vreducesd $0x12,%xmm28,%xmm29,%xmm30");

	/* AVX-512: Op code 0f 3a 66 */

	asm volatile("vfpclassps $0x12,%zmm27,%k5");
	asm volatile("vfpclasspd $0x12,%zmm30,%k5");

	/* AVX-512: Op code 0f 3a 67 */

	asm volatile("vfpclassss $0x12,%xmm27,%k5");
	asm volatile("vfpclasssd $0x12,%xmm30,%k5");

	/* AVX-512: Op code 0f 3a 70 */

	asm volatile("vpshldw $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldw $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldw $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshldw $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 71 */

	asm volatile("vpshldd $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldd $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldd $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshldd $0x12,%zmm25,%zmm26,%zmm27");

	asm volatile("vpshldq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldq $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshldq $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 72 */

	asm volatile("vpshrdw $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdw $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdw $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshrdw $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a 73 */

	asm volatile("vpshrdd $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdd $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdd $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshrdd $0x12,%zmm25,%zmm26,%zmm27");

	asm volatile("vpshrdq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdq $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vpshrdq $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a ce */

	asm volatile("gf2p8affineqb $0x12,%xmm1,%xmm3");

	asm volatile("vgf2p8affineqb $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vgf2p8affineqb $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vgf2p8affineqb $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vgf2p8affineqb $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 3a cf */

	asm volatile("gf2p8affineinvqb $0x12,%xmm1,%xmm3");

	asm volatile("vgf2p8affineinvqb $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vgf2p8affineinvqb $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vgf2p8affineinvqb $0x12,%zmm1,%zmm2,%zmm3");
	asm volatile("vgf2p8affineinvqb $0x12,%zmm25,%zmm26,%zmm27");

	/* AVX-512: Op code 0f 72 (Grp13) */

	asm volatile("vprord $0x12,%zmm25,%zmm26");
	asm volatile("vprorq $0x12,%zmm25,%zmm26");
	asm volatile("vprold $0x12,%zmm29,%zmm30");
	asm volatile("vprolq $0x12,%zmm29,%zmm30");
	asm volatile("psrad  $0x2,%mm6");
	asm volatile("vpsrad $0x5,%ymm6,%ymm2");
	asm volatile("vpsrad $0x5,%zmm26,%zmm22");
	asm volatile("vpsraq $0x5,%zmm26,%zmm22");

	/* AVX-512: Op code 0f 38 c6 (Grp18) */

	asm volatile("vgatherpf0dps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vgatherpf0dpd 0x7b(%r14,%ymm31,8){%k1}");
	asm volatile("vgatherpf1dps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vgatherpf1dpd 0x7b(%r14,%ymm31,8){%k1}");
	asm volatile("vscatterpf0dps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf0dpd 0x7b(%r14,%ymm31,8){%k1}");
	asm volatile("vscatterpf1dps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf1dpd 0x7b(%r14,%ymm31,8){%k1}");

	/* AVX-512: Op code 0f 38 c7 (Grp19) */

	asm volatile("vgatherpf0qps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vgatherpf0qpd 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vgatherpf1qps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vgatherpf1qpd 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf0qps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf0qpd 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf1qps 0x7b(%r14,%zmm31,8){%k1}");
	asm volatile("vscatterpf1qpd 0x7b(%r14,%zmm31,8){%k1}");

	/* AVX-512: Examples */

	asm volatile("vaddpd %zmm28,%zmm29,%zmm30");
	asm volatile("vaddpd %zmm28,%zmm29,%zmm30{%k7}");
	asm volatile("vaddpd %zmm28,%zmm29,%zmm30{%k7}{z}");
	asm volatile("vaddpd {rn-sae},%zmm28,%zmm29,%zmm30");
	asm volatile("vaddpd {ru-sae},%zmm28,%zmm29,%zmm30");
	asm volatile("vaddpd {rd-sae},%zmm28,%zmm29,%zmm30");
	asm volatile("vaddpd {rz-sae},%zmm28,%zmm29,%zmm30");
	asm volatile("vaddpd (%rcx),%zmm29,%zmm30");
	asm volatile("vaddpd 0x123(%rax,%r14,8),%zmm29,%zmm30");
	asm volatile("vaddpd (%rcx){1to8},%zmm29,%zmm30");
	asm volatile("vaddpd 0x1fc0(%rdx),%zmm29,%zmm30");
	asm volatile("vaddpd 0x3f8(%rdx){1to8},%zmm29,%zmm30");
	asm volatile("vcmpeq_uqps 0x1fc(%rdx){1to16},%zmm30,%k5");
	asm volatile("vcmpltsd 0x123(%rax,%r14,8),%xmm29,%k5{%k7}");
	asm volatile("vcmplesd {sae},%xmm28,%xmm29,%k5{%k7}");
	asm volatile("vgetmantss $0x5b,0x123(%rax,%r14,8),%xmm29,%xmm30{%k7}");

	/* bndmk m64, bnd */

	asm volatile("bndmk (%rax), %bnd0");
	asm volatile("bndmk (%r8), %bnd0");
	asm volatile("bndmk (0x12345678), %bnd0");
	asm volatile("bndmk (%rax), %bnd3");
	asm volatile("bndmk (%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndmk (%rax,%rcx,1), %bnd0");
	asm volatile("bndmk (%rax,%rcx,8), %bnd0");
	asm volatile("bndmk 0x12(%rax), %bnd0");
	asm volatile("bndmk 0x12(%rbp), %bnd0");
	asm volatile("bndmk 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndmk 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndmk 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndmk 0x12345678(%rax), %bnd0");
	asm volatile("bndmk 0x12345678(%rbp), %bnd0");
	asm volatile("bndmk 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndmk 0x12345678(%rax,%rcx,8), %bnd0");

	/* bndcl r/m64, bnd */

	asm volatile("bndcl (%rax), %bnd0");
	asm volatile("bndcl (%r8), %bnd0");
	asm volatile("bndcl (0x12345678), %bnd0");
	asm volatile("bndcl (%rax), %bnd3");
	asm volatile("bndcl (%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcl (%rax,%rcx,1), %bnd0");
	asm volatile("bndcl (%rax,%rcx,8), %bnd0");
	asm volatile("bndcl 0x12(%rax), %bnd0");
	asm volatile("bndcl 0x12(%rbp), %bnd0");
	asm volatile("bndcl 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcl 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcl 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcl 0x12345678(%rax), %bnd0");
	asm volatile("bndcl 0x12345678(%rbp), %bnd0");
	asm volatile("bndcl 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcl 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcl %rax, %bnd0");

	/* bndcu r/m64, bnd */

	asm volatile("bndcu (%rax), %bnd0");
	asm volatile("bndcu (%r8), %bnd0");
	asm volatile("bndcu (0x12345678), %bnd0");
	asm volatile("bndcu (%rax), %bnd3");
	asm volatile("bndcu (%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcu (%rax,%rcx,1), %bnd0");
	asm volatile("bndcu (%rax,%rcx,8), %bnd0");
	asm volatile("bndcu 0x12(%rax), %bnd0");
	asm volatile("bndcu 0x12(%rbp), %bnd0");
	asm volatile("bndcu 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcu 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcu 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcu 0x12345678(%rax), %bnd0");
	asm volatile("bndcu 0x12345678(%rbp), %bnd0");
	asm volatile("bndcu 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcu 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcu %rax, %bnd0");

	/* bndcn r/m64, bnd */

	asm volatile("bndcn (%rax), %bnd0");
	asm volatile("bndcn (%r8), %bnd0");
	asm volatile("bndcn (0x12345678), %bnd0");
	asm volatile("bndcn (%rax), %bnd3");
	asm volatile("bndcn (%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndcn (%rax,%rcx,1), %bnd0");
	asm volatile("bndcn (%rax,%rcx,8), %bnd0");
	asm volatile("bndcn 0x12(%rax), %bnd0");
	asm volatile("bndcn 0x12(%rbp), %bnd0");
	asm volatile("bndcn 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndcn 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndcn 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndcn 0x12345678(%rax), %bnd0");
	asm volatile("bndcn 0x12345678(%rbp), %bnd0");
	asm volatile("bndcn 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndcn 0x12345678(%rax,%rcx,8), %bnd0");
	asm volatile("bndcn %rax, %bnd0");

	/* bndmov m128, bnd */

	asm volatile("bndmov (%rax), %bnd0");
	asm volatile("bndmov (%r8), %bnd0");
	asm volatile("bndmov (0x12345678), %bnd0");
	asm volatile("bndmov (%rax), %bnd3");
	asm volatile("bndmov (%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndmov (%rax,%rcx,1), %bnd0");
	asm volatile("bndmov (%rax,%rcx,8), %bnd0");
	asm volatile("bndmov 0x12(%rax), %bnd0");
	asm volatile("bndmov 0x12(%rbp), %bnd0");
	asm volatile("bndmov 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndmov 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndmov 0x12(%rax,%rcx,8), %bnd0");
	asm volatile("bndmov 0x12345678(%rax), %bnd0");
	asm volatile("bndmov 0x12345678(%rbp), %bnd0");
	asm volatile("bndmov 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rax,%rcx,1), %bnd0");
	asm volatile("bndmov 0x12345678(%rax,%rcx,8), %bnd0");

	/* bndmov bnd, m128 */

	asm volatile("bndmov %bnd0, (%rax)");
	asm volatile("bndmov %bnd0, (%r8)");
	asm volatile("bndmov %bnd0, (0x12345678)");
	asm volatile("bndmov %bnd3, (%rax)");
	asm volatile("bndmov %bnd0, (%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(,%rax,1)");
	asm volatile("bndmov %bnd0, (%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, (%rax,%rcx,8)");
	asm volatile("bndmov %bnd0, 0x12(%rax)");
	asm volatile("bndmov %bnd0, 0x12(%rbp)");
	asm volatile("bndmov %bnd0, 0x12(%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12(%rbp,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12(%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, 0x12(%rax,%rcx,8)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax)");
	asm volatile("bndmov %bnd0, 0x12345678(%rbp)");
	asm volatile("bndmov %bnd0, 0x12345678(%rcx,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rbp,%rax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax,%rcx,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%rax,%rcx,8)");

	/* bndmov bnd2, bnd1 */

	asm volatile("bndmov %bnd0, %bnd1");
	asm volatile("bndmov %bnd1, %bnd0");

	/* bndldx mib, bnd */

	asm volatile("bndldx (%rax), %bnd0");
	asm volatile("bndldx (%r8), %bnd0");
	asm volatile("bndldx (0x12345678), %bnd0");
	asm volatile("bndldx (%rax), %bnd3");
	asm volatile("bndldx (%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(,%rax,1), %bnd0");
	asm volatile("bndldx (%rax,%rcx,1), %bnd0");
	asm volatile("bndldx 0x12(%rax), %bnd0");
	asm volatile("bndldx 0x12(%rbp), %bnd0");
	asm volatile("bndldx 0x12(%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12(%rbp,%rax,1), %bnd0");
	asm volatile("bndldx 0x12(%rax,%rcx,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rax), %bnd0");
	asm volatile("bndldx 0x12345678(%rbp), %bnd0");
	asm volatile("bndldx 0x12345678(%rcx,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rbp,%rax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%rax,%rcx,1), %bnd0");

	/* bndstx bnd, mib */

	asm volatile("bndstx %bnd0, (%rax)");
	asm volatile("bndstx %bnd0, (%r8)");
	asm volatile("bndstx %bnd0, (0x12345678)");
	asm volatile("bndstx %bnd3, (%rax)");
	asm volatile("bndstx %bnd0, (%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(,%rax,1)");
	asm volatile("bndstx %bnd0, (%rax,%rcx,1)");
	asm volatile("bndstx %bnd0, 0x12(%rax)");
	asm volatile("bndstx %bnd0, 0x12(%rbp)");
	asm volatile("bndstx %bnd0, 0x12(%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12(%rbp,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12(%rax,%rcx,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rax)");
	asm volatile("bndstx %bnd0, 0x12345678(%rbp)");
	asm volatile("bndstx %bnd0, 0x12345678(%rcx,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rbp,%rax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%rax,%rcx,1)");

	/* bnd prefix on call, ret, jmp and all jcc */

	asm volatile("bnd call label1");  /* Expecting: call unconditional 0 */
	asm volatile("bnd call *(%eax)"); /* Expecting: call indirect      0 */
	asm volatile("bnd ret");          /* Expecting: ret  indirect      0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0 */
	asm volatile("bnd jmp *(%ecx)");  /* Expecting: jmp  indirect      0 */
	asm volatile("bnd jne label1");   /* Expecting: jcc  conditional   0 */

	/* sha1rnds4 imm8, xmm2/m128, xmm1 */

	asm volatile("sha1rnds4 $0x0, %xmm1, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm2");
	asm volatile("sha1rnds4 $0x91, %xmm8, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm8");
	asm volatile("sha1rnds4 $0x91, %xmm15, %xmm8");
	asm volatile("sha1rnds4 $0x91, (%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%r8), %xmm0");
	asm volatile("sha1rnds4 $0x91, (0x12345678), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax), %xmm3");
	asm volatile("sha1rnds4 $0x91, (%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rbp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rbp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1nexte xmm2/m128, xmm1 */

	asm volatile("sha1nexte %xmm1, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm2");
	asm volatile("sha1nexte %xmm8, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm8");
	asm volatile("sha1nexte %xmm15, %xmm8");
	asm volatile("sha1nexte (%rax), %xmm0");
	asm volatile("sha1nexte (%r8), %xmm0");
	asm volatile("sha1nexte (0x12345678), %xmm0");
	asm volatile("sha1nexte (%rax), %xmm3");
	asm volatile("sha1nexte (%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1nexte (%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte (%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12(%rax), %xmm0");
	asm volatile("sha1nexte 0x12(%rbp), %xmm0");
	asm volatile("sha1nexte 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rbp), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1msg1 xmm2/m128, xmm1 */

	asm volatile("sha1msg1 %xmm1, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm2");
	asm volatile("sha1msg1 %xmm8, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm8");
	asm volatile("sha1msg1 %xmm15, %xmm8");
	asm volatile("sha1msg1 (%rax), %xmm0");
	asm volatile("sha1msg1 (%r8), %xmm0");
	asm volatile("sha1msg1 (0x12345678), %xmm0");
	asm volatile("sha1msg1 (%rax), %xmm3");
	asm volatile("sha1msg1 (%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1msg1 (%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 (%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12(%rax), %xmm0");
	asm volatile("sha1msg1 0x12(%rbp), %xmm0");
	asm volatile("sha1msg1 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rbp), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha1msg2 xmm2/m128, xmm1 */

	asm volatile("sha1msg2 %xmm1, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm2");
	asm volatile("sha1msg2 %xmm8, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm8");
	asm volatile("sha1msg2 %xmm15, %xmm8");
	asm volatile("sha1msg2 (%rax), %xmm0");
	asm volatile("sha1msg2 (%r8), %xmm0");
	asm volatile("sha1msg2 (0x12345678), %xmm0");
	asm volatile("sha1msg2 (%rax), %xmm3");
	asm volatile("sha1msg2 (%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha1msg2 (%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 (%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12(%rax), %xmm0");
	asm volatile("sha1msg2 0x12(%rbp), %xmm0");
	asm volatile("sha1msg2 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rbp), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256rnds2 <XMM0>, xmm2/m128, xmm1 */
	/* Note sha256rnds2 has an implicit operand 'xmm0' */

	asm volatile("sha256rnds2 %xmm4, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm2");
	asm volatile("sha256rnds2 %xmm8, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm8");
	asm volatile("sha256rnds2 %xmm15, %xmm8");
	asm volatile("sha256rnds2 (%rax), %xmm1");
	asm volatile("sha256rnds2 (%r8), %xmm1");
	asm volatile("sha256rnds2 (0x12345678), %xmm1");
	asm volatile("sha256rnds2 (%rax), %xmm3");
	asm volatile("sha256rnds2 (%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(,%rax,1), %xmm1");
	asm volatile("sha256rnds2 (%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 (%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax), %xmm1");
	asm volatile("sha256rnds2 0x12(%rbp), %xmm1");
	asm volatile("sha256rnds2 0x12(%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rbp,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rbp), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rcx,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rbp,%rax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256msg1 xmm2/m128, xmm1 */

	asm volatile("sha256msg1 %xmm1, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm2");
	asm volatile("sha256msg1 %xmm8, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm8");
	asm volatile("sha256msg1 %xmm15, %xmm8");
	asm volatile("sha256msg1 (%rax), %xmm0");
	asm volatile("sha256msg1 (%r8), %xmm0");
	asm volatile("sha256msg1 (0x12345678), %xmm0");
	asm volatile("sha256msg1 (%rax), %xmm3");
	asm volatile("sha256msg1 (%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha256msg1 (%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 (%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12(%rax), %xmm0");
	asm volatile("sha256msg1 0x12(%rbp), %xmm0");
	asm volatile("sha256msg1 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rbp), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%rax,%rcx,8), %xmm15");

	/* sha256msg2 xmm2/m128, xmm1 */

	asm volatile("sha256msg2 %xmm1, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm2");
	asm volatile("sha256msg2 %xmm8, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm8");
	asm volatile("sha256msg2 %xmm15, %xmm8");
	asm volatile("sha256msg2 (%rax), %xmm0");
	asm volatile("sha256msg2 (%r8), %xmm0");
	asm volatile("sha256msg2 (0x12345678), %xmm0");
	asm volatile("sha256msg2 (%rax), %xmm3");
	asm volatile("sha256msg2 (%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(,%rax,1), %xmm0");
	asm volatile("sha256msg2 (%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 (%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12(%rax), %xmm0");
	asm volatile("sha256msg2 0x12(%rbp), %xmm0");
	asm volatile("sha256msg2 0x12(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 0x12(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rbp), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rcx,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rbp,%rax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%rax,%rcx,8), %xmm15");

	/* clflushopt m8 */

	asm volatile("clflushopt (%rax)");
	asm volatile("clflushopt (%r8)");
	asm volatile("clflushopt (0x12345678)");
	asm volatile("clflushopt 0x12345678(%rax,%rcx,8)");
	asm volatile("clflushopt 0x12345678(%r8,%rcx,8)");
	/* Also check instructions in the same group encoding as clflushopt */
	asm volatile("clflush (%rax)");
	asm volatile("clflush (%r8)");
	asm volatile("sfence");

	/* clwb m8 */

	asm volatile("clwb (%rax)");
	asm volatile("clwb (%r8)");
	asm volatile("clwb (0x12345678)");
	asm volatile("clwb 0x12345678(%rax,%rcx,8)");
	asm volatile("clwb 0x12345678(%r8,%rcx,8)");
	/* Also check instructions in the same group encoding as clwb */
	asm volatile("xsaveopt (%rax)");
	asm volatile("xsaveopt (%r8)");
	asm volatile("mfence");

	/* cldemote m8 */

	asm volatile("cldemote (%rax)");
	asm volatile("cldemote (%r8)");
	asm volatile("cldemote (0x12345678)");
	asm volatile("cldemote 0x12345678(%rax,%rcx,8)");
	asm volatile("cldemote 0x12345678(%r8,%rcx,8)");

	/* xsavec mem */

	asm volatile("xsavec (%rax)");
	asm volatile("xsavec (%r8)");
	asm volatile("xsavec (0x12345678)");
	asm volatile("xsavec 0x12345678(%rax,%rcx,8)");
	asm volatile("xsavec 0x12345678(%r8,%rcx,8)");

	/* xsaves mem */

	asm volatile("xsaves (%rax)");
	asm volatile("xsaves (%r8)");
	asm volatile("xsaves (0x12345678)");
	asm volatile("xsaves 0x12345678(%rax,%rcx,8)");
	asm volatile("xsaves 0x12345678(%r8,%rcx,8)");

	/* xrstors mem */

	asm volatile("xrstors (%rax)");
	asm volatile("xrstors (%r8)");
	asm volatile("xrstors (0x12345678)");
	asm volatile("xrstors 0x12345678(%rax,%rcx,8)");
	asm volatile("xrstors 0x12345678(%r8,%rcx,8)");

	/* ptwrite */

	asm volatile("ptwrite (%rax)");
	asm volatile("ptwrite (%r8)");
	asm volatile("ptwrite (0x12345678)");
	asm volatile("ptwrite 0x12345678(%rax,%rcx,8)");
	asm volatile("ptwrite 0x12345678(%r8,%rcx,8)");

	asm volatile("ptwritel (%rax)");
	asm volatile("ptwritel (%r8)");
	asm volatile("ptwritel (0x12345678)");
	asm volatile("ptwritel 0x12345678(%rax,%rcx,8)");
	asm volatile("ptwritel 0x12345678(%r8,%rcx,8)");

	asm volatile("ptwriteq (%rax)");
	asm volatile("ptwriteq (%r8)");
	asm volatile("ptwriteq (0x12345678)");
	asm volatile("ptwriteq 0x12345678(%rax,%rcx,8)");
	asm volatile("ptwriteq 0x12345678(%r8,%rcx,8)");

	/* tpause */

	asm volatile("tpause %ebx");
	asm volatile("tpause %r8d");

	/* umonitor */

	asm volatile("umonitor %eax");
	asm volatile("umonitor %rax");
	asm volatile("umonitor %r8d");

	/* umwait */

	asm volatile("umwait %eax");
	asm volatile("umwait %r8d");

	/* movdiri */

	asm volatile("movdiri %rax,(%rbx)");
	asm volatile("movdiri %rcx,0x12345678(%rax)");

	/* movdir64b */

	asm volatile("movdir64b (%rax),%rbx");
	asm volatile("movdir64b 0x12345678(%rax),%rcx");
	asm volatile("movdir64b (%eax),%ebx");
	asm volatile("movdir64b 0x12345678(%eax),%ecx");

	/* enqcmd */

	asm volatile("enqcmd (%rax),%rbx");
	asm volatile("enqcmd 0x12345678(%rax),%rcx");
	asm volatile("enqcmd (%eax),%ebx");
	asm volatile("enqcmd 0x12345678(%eax),%ecx");

	/* enqcmds */

	asm volatile("enqcmds (%rax),%rbx");
	asm volatile("enqcmds 0x12345678(%rax),%rcx");
	asm volatile("enqcmds (%eax),%ebx");
	asm volatile("enqcmds 0x12345678(%eax),%ecx");

	/* incsspd/q */

	asm volatile("incsspd %eax");
	asm volatile("incsspd %r8d");
	asm volatile("incsspq %rax");
	asm volatile("incsspq %r8");
	/* Also check instructions in the same group encoding as incsspd/q */
	asm volatile("xrstor (%rax)");
	asm volatile("xrstor (%r8)");
	asm volatile("xrstor (0x12345678)");
	asm volatile("xrstor 0x12345678(%rax,%rcx,8)");
	asm volatile("xrstor 0x12345678(%r8,%rcx,8)");
	asm volatile("lfence");

	/* rdsspd/q */

	asm volatile("rdsspd %eax");
	asm volatile("rdsspd %r8d");
	asm volatile("rdsspq %rax");
	asm volatile("rdsspq %r8");

	/* saveprevssp */

	asm volatile("saveprevssp");

	/* rstorssp */

	asm volatile("rstorssp (%rax)");
	asm volatile("rstorssp (%r8)");
	asm volatile("rstorssp (0x12345678)");
	asm volatile("rstorssp 0x12345678(%rax,%rcx,8)");
	asm volatile("rstorssp 0x12345678(%r8,%rcx,8)");

	/* wrssd/q */

	asm volatile("wrssd %ecx,(%rax)");
	asm volatile("wrssd %edx,(%r8)");
	asm volatile("wrssd %edx,(0x12345678)");
	asm volatile("wrssd %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("wrssd %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("wrssq %rcx,(%rax)");
	asm volatile("wrssq %rdx,(%r8)");
	asm volatile("wrssq %rdx,(0x12345678)");
	asm volatile("wrssq %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("wrssq %rdx,0x12345678(%r8,%rcx,8)");

	/* wrussd/q */

	asm volatile("wrussd %ecx,(%rax)");
	asm volatile("wrussd %edx,(%r8)");
	asm volatile("wrussd %edx,(0x12345678)");
	asm volatile("wrussd %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("wrussd %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("wrussq %rcx,(%rax)");
	asm volatile("wrussq %rdx,(%r8)");
	asm volatile("wrussq %rdx,(0x12345678)");
	asm volatile("wrussq %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("wrussq %rdx,0x12345678(%r8,%rcx,8)");

	/* setssbsy */

	asm volatile("setssbsy");
	/* Also check instructions in the same group encoding as setssbsy */
	asm volatile("rdpkru");
	asm volatile("wrpkru");

	/* clrssbsy */

	asm volatile("clrssbsy (%rax)");
	asm volatile("clrssbsy (%r8)");
	asm volatile("clrssbsy (0x12345678)");
	asm volatile("clrssbsy 0x12345678(%rax,%rcx,8)");
	asm volatile("clrssbsy 0x12345678(%r8,%rcx,8)");

	/* endbr32/64 */

	asm volatile("endbr32");
	asm volatile("endbr64");

	/* call with/without notrack prefix */

	asm volatile("callq *%rax");				/* Expecting: call indirect 0 */
	asm volatile("callq *(%rax)");				/* Expecting: call indirect 0 */
	asm volatile("callq *(%r8)");				/* Expecting: call indirect 0 */
	asm volatile("callq *(0x12345678)");			/* Expecting: call indirect 0 */
	asm volatile("callq *0x12345678(%rax,%rcx,8)");		/* Expecting: call indirect 0 */
	asm volatile("callq *0x12345678(%r8,%rcx,8)");		/* Expecting: call indirect 0 */

	asm volatile("bnd callq *%rax");			/* Expecting: call indirect 0 */
	asm volatile("bnd callq *(%rax)");			/* Expecting: call indirect 0 */
	asm volatile("bnd callq *(%r8)");			/* Expecting: call indirect 0 */
	asm volatile("bnd callq *(0x12345678)");		/* Expecting: call indirect 0 */
	asm volatile("bnd callq *0x12345678(%rax,%rcx,8)");	/* Expecting: call indirect 0 */
	asm volatile("bnd callq *0x12345678(%r8,%rcx,8)");	/* Expecting: call indirect 0 */

	asm volatile("notrack callq *%rax");			/* Expecting: call indirect 0 */
	asm volatile("notrack callq *(%rax)");			/* Expecting: call indirect 0 */
	asm volatile("notrack callq *(%r8)");			/* Expecting: call indirect 0 */
	asm volatile("notrack callq *(0x12345678)");		/* Expecting: call indirect 0 */
	asm volatile("notrack callq *0x12345678(%rax,%rcx,8)");	/* Expecting: call indirect 0 */
	asm volatile("notrack callq *0x12345678(%r8,%rcx,8)");	/* Expecting: call indirect 0 */

	asm volatile("notrack bnd callq *%rax");		/* Expecting: call indirect 0 */
	asm volatile("notrack bnd callq *(%rax)");		/* Expecting: call indirect 0 */
	asm volatile("notrack bnd callq *(%r8)");		/* Expecting: call indirect 0 */
	asm volatile("notrack bnd callq *(0x12345678)");	/* Expecting: call indirect 0 */
	asm volatile("notrack bnd callq *0x12345678(%rax,%rcx,8)");	/* Expecting: call indirect 0 */
	asm volatile("notrack bnd callq *0x12345678(%r8,%rcx,8)");	/* Expecting: call indirect 0 */

	/* jmp with/without notrack prefix */

	asm volatile("jmpq *%rax");				/* Expecting: jmp indirect 0 */
	asm volatile("jmpq *(%rax)");				/* Expecting: jmp indirect 0 */
	asm volatile("jmpq *(%r8)");				/* Expecting: jmp indirect 0 */
	asm volatile("jmpq *(0x12345678)");			/* Expecting: jmp indirect 0 */
	asm volatile("jmpq *0x12345678(%rax,%rcx,8)");		/* Expecting: jmp indirect 0 */
	asm volatile("jmpq *0x12345678(%r8,%rcx,8)");		/* Expecting: jmp indirect 0 */

	asm volatile("bnd jmpq *%rax");				/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmpq *(%rax)");			/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmpq *(%r8)");			/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmpq *(0x12345678)");			/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmpq *0x12345678(%rax,%rcx,8)");	/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmpq *0x12345678(%r8,%rcx,8)");	/* Expecting: jmp indirect 0 */

	asm volatile("notrack jmpq *%rax");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmpq *(%rax)");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmpq *(%r8)");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmpq *(0x12345678)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmpq *0x12345678(%rax,%rcx,8)");	/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmpq *0x12345678(%r8,%rcx,8)");	/* Expecting: jmp indirect 0 */

	asm volatile("notrack bnd jmpq *%rax");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmpq *(%rax)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmpq *(%r8)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmpq *(0x12345678)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmpq *0x12345678(%rax,%rcx,8)");	/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmpq *0x12345678(%r8,%rcx,8)");	/* Expecting: jmp indirect 0 */

	/* AMX */

	asm volatile("ldtilecfg (%rax,%rcx,8)");
	asm volatile("ldtilecfg (%r8,%rcx,8)");
	asm volatile("sttilecfg (%rax,%rcx,8)");
	asm volatile("sttilecfg (%r8,%rcx,8)");
	asm volatile("tdpbf16ps %tmm0, %tmm1, %tmm2");
	asm volatile("tdpbssd %tmm0, %tmm1, %tmm2");
	asm volatile("tdpbsud %tmm0, %tmm1, %tmm2");
	asm volatile("tdpbusd %tmm0, %tmm1, %tmm2");
	asm volatile("tdpbuud %tmm0, %tmm1, %tmm2");
	asm volatile("tileloadd (%rax,%rcx,8), %tmm1");
	asm volatile("tileloadd (%r8,%rcx,8), %tmm2");
	asm volatile("tileloaddt1 (%rax,%rcx,8), %tmm1");
	asm volatile("tileloaddt1 (%r8,%rcx,8), %tmm2");
	asm volatile("tilerelease");
	asm volatile("tilestored %tmm1, (%rax,%rcx,8)");
	asm volatile("tilestored %tmm2, (%r8,%rcx,8)");
	asm volatile("tilezero %tmm0");
	asm volatile("tilezero %tmm7");

	/* User Interrupt */

	asm volatile("clui");
	asm volatile("senduipi %rax");
	asm volatile("senduipi %r8");
	asm volatile("stui");
	asm volatile("testui");
	asm volatile("uiret");

	/* AVX512-FP16 */

	asm volatile("vaddph %zmm3, %zmm2, %zmm1");
	asm volatile("vaddph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vaddph %xmm3, %xmm2, %xmm1");
	asm volatile("vaddph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vaddph %ymm3, %ymm2, %ymm1");
	asm volatile("vaddph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vaddsh %xmm3, %xmm2, %xmm1");
	asm volatile("vaddsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vaddsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcmpph $0x12, %zmm3, %zmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%rax,%rcx,8), %zmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %zmm2, %k5");
	asm volatile("vcmpph $0x12, %xmm3, %xmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%rax,%rcx,8), %xmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %k5");
	asm volatile("vcmpph $0x12, %ymm3, %ymm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%rax,%rcx,8), %ymm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %ymm2, %k5");
	asm volatile("vcmpsh $0x12, %xmm3, %xmm2, %k5");
	asm volatile("vcmpsh $0x12, 0x12345678(%rax,%rcx,8), %xmm2, %k5");
	asm volatile("vcmpsh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %k5");
	asm volatile("vcomish %xmm2, %xmm1");
	asm volatile("vcomish 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcomish 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtdq2ph %zmm2, %ymm1");
	asm volatile("vcvtdq2ph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtdq2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtdq2ph %xmm2, %xmm1");
	asm volatile("vcvtdq2ph %ymm2, %xmm1");
	asm volatile("vcvtpd2ph %zmm2, %xmm1");
	asm volatile("vcvtpd2ph %xmm2, %xmm1");
	asm volatile("vcvtpd2ph %ymm2, %xmm1");
	asm volatile("vcvtph2dq %ymm2, %zmm1");
	asm volatile("vcvtph2dq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2dq %xmm2, %xmm1");
	asm volatile("vcvtph2dq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2dq %xmm2, %ymm1");
	asm volatile("vcvtph2dq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2pd %xmm2, %zmm1");
	asm volatile("vcvtph2pd 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2pd %xmm2, %xmm1");
	asm volatile("vcvtph2pd 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2pd %xmm2, %ymm1");
	asm volatile("vcvtph2pd 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2ps %ymm2, %zmm1");
	asm volatile("vcvtph2ps 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2ps %xmm2, %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2ps %xmm2, %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2ps %xmm2, %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2ps %xmm2, %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2psx %ymm2, %zmm1");
	asm volatile("vcvtph2psx 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2psx %xmm2, %xmm1");
	asm volatile("vcvtph2psx 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2psx %xmm2, %ymm1");
	asm volatile("vcvtph2psx 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2qq %xmm2, %zmm1");
	asm volatile("vcvtph2qq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2qq %xmm2, %xmm1");
	asm volatile("vcvtph2qq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2qq %xmm2, %ymm1");
	asm volatile("vcvtph2qq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2udq %ymm2, %zmm1");
	asm volatile("vcvtph2udq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2udq %xmm2, %xmm1");
	asm volatile("vcvtph2udq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2udq %xmm2, %ymm1");
	asm volatile("vcvtph2udq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2uqq %xmm2, %zmm1");
	asm volatile("vcvtph2uqq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2uqq %xmm2, %xmm1");
	asm volatile("vcvtph2uqq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2uqq %xmm2, %ymm1");
	asm volatile("vcvtph2uqq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2uw %zmm2, %zmm1");
	asm volatile("vcvtph2uw 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2uw %xmm2, %xmm1");
	asm volatile("vcvtph2uw 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2uw %ymm2, %ymm1");
	asm volatile("vcvtph2uw 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2w %zmm2, %zmm1");
	asm volatile("vcvtph2w 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2w %xmm2, %xmm1");
	asm volatile("vcvtph2w 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2w %ymm2, %ymm1");
	asm volatile("vcvtph2w 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtps2ph $0x12, %zmm1, 0x12345678(%rax,%rcx,8)");
	asm volatile("vcvtps2ph $0x12, %zmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %zmm2, %ymm1");
	asm volatile("vcvtps2ph $0x12, %ymm1, 0x12345678(%rax,%rcx,8)");
	asm volatile("vcvtps2ph $0x12, %ymm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm1, 0x12345678(%rax,%rcx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, 0x12345678(%rax,%rcx,8)");
	asm volatile("vcvtps2ph $0x12, %ymm2, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %xmm2, 0x12345678(%rax,%rcx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm2, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2phx %zmm2, %ymm1");
	asm volatile("vcvtps2phx 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtps2phx 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtps2phx %xmm2, %xmm1");
	asm volatile("vcvtps2phx %ymm2, %xmm1");
	asm volatile("vcvtqq2ph %zmm2, %xmm1");
	asm volatile("vcvtqq2ph %xmm2, %xmm1");
	asm volatile("vcvtqq2ph %ymm2, %xmm1");
	asm volatile("vcvtsd2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2sd 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2si 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvtsh2si 0x12345678(%eax,%ecx,8), %rax");
	asm volatile("vcvtsh2ss 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2usi %xmm1, %eax");
	asm volatile("vcvtsh2usi 0x12345678(%rax,%rcx,8), %eax");
	asm volatile("vcvtsh2usi 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvtsh2usi %xmm1, %rax");
	asm volatile("vcvtsh2usi 0x12345678(%rax,%rcx,8), %rax");
	asm volatile("vcvtsh2usi 0x12345678(%eax,%ecx,8), %rax");
	asm volatile("vcvtsi2sh %eax, %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsi2sh %rax, %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtss2sh %xmm3, %xmm2, %xmm1");
	asm volatile("vcvtss2sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vcvtss2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvttph2dq %ymm2, %zmm1");
	asm volatile("vcvttph2dq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2dq %xmm2, %xmm1");
	asm volatile("vcvttph2dq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2dq %xmm2, %ymm1");
	asm volatile("vcvttph2dq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2qq %xmm2, %zmm1");
	asm volatile("vcvttph2qq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2qq %xmm2, %xmm1");
	asm volatile("vcvttph2qq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2qq %xmm2, %ymm1");
	asm volatile("vcvttph2qq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2udq %ymm2, %zmm1");
	asm volatile("vcvttph2udq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2udq %xmm2, %xmm1");
	asm volatile("vcvttph2udq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2udq %xmm2, %ymm1");
	asm volatile("vcvttph2udq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2uqq %xmm2, %zmm1");
	asm volatile("vcvttph2uqq 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2uqq %xmm2, %xmm1");
	asm volatile("vcvttph2uqq 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2uqq %xmm2, %ymm1");
	asm volatile("vcvttph2uqq 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2uw %zmm2, %zmm1");
	asm volatile("vcvttph2uw 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2uw %xmm2, %xmm1");
	asm volatile("vcvttph2uw 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2uw %ymm2, %ymm1");
	asm volatile("vcvttph2uw 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2w %zmm2, %zmm1");
	asm volatile("vcvttph2w 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2w %xmm2, %xmm1");
	asm volatile("vcvttph2w 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2w %ymm2, %ymm1");
	asm volatile("vcvttph2w 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttsh2si %xmm1, %eax");
	asm volatile("vcvttsh2si 0x12345678(%rax,%rcx,8), %eax");
	asm volatile("vcvttsh2si 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvttsh2si %xmm1, %rax");
	asm volatile("vcvttsh2si 0x12345678(%rax,%rcx,8), %rax");
	asm volatile("vcvttsh2si 0x12345678(%eax,%ecx,8), %rax");
	asm volatile("vcvttsh2usi %xmm1, %eax");
	asm volatile("vcvttsh2usi 0x12345678(%rax,%rcx,8), %eax");
	asm volatile("vcvttsh2usi 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvttsh2usi %xmm1, %rax");
	asm volatile("vcvttsh2usi 0x12345678(%rax,%rcx,8), %rax");
	asm volatile("vcvttsh2usi 0x12345678(%eax,%ecx,8), %rax");
	asm volatile("vcvtudq2ph %zmm2, %ymm1");
	asm volatile("vcvtudq2ph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtudq2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtudq2ph %xmm2, %xmm1");
	asm volatile("vcvtudq2ph %ymm2, %xmm1");
	asm volatile("vcvtuqq2ph %zmm2, %xmm1");
	asm volatile("vcvtuqq2ph %xmm2, %xmm1");
	asm volatile("vcvtuqq2ph %ymm2, %xmm1");
	asm volatile("vcvtusi2sh %eax, %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtusi2sh %rax, %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtuw2ph %zmm2, %zmm1");
	asm volatile("vcvtuw2ph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtuw2ph %xmm2, %xmm1");
	asm volatile("vcvtuw2ph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtuw2ph %ymm2, %ymm1");
	asm volatile("vcvtuw2ph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtw2ph %zmm2, %zmm1");
	asm volatile("vcvtw2ph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtw2ph %xmm2, %xmm1");
	asm volatile("vcvtw2ph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtw2ph %ymm2, %ymm1");
	asm volatile("vcvtw2ph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vdivph %zmm3, %zmm2, %zmm1");
	asm volatile("vdivph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vdivph %xmm3, %xmm2, %xmm1");
	asm volatile("vdivph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vdivph %ymm3, %ymm2, %ymm1");
	asm volatile("vdivph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vdivsh %xmm3, %xmm2, %xmm1");
	asm volatile("vdivsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vdivsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfcmaddcph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfcmaddcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmaddcph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfcmaddcph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfcmaddcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmaddcsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfcmulcph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfcmulcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmulcph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfcmulcph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfcmulcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmulcsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd132sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd213sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd231sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddcph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddcph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddcph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddcsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub132sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub213sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub231sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmulcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmulcph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmulcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmulcph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmulcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmulcph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmulcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmulcsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfmulcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd132sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd213sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd231sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub132ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub132ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub132ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub132sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub213ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub213ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub213ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub213sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub231ph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub231ph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub231ph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub231sh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfpclassph $0x12, %zmm1, %k5");
	asm volatile("vfpclassph $0x12, %xmm1, %k5");
	asm volatile("vfpclassph $0x12, %ymm1, %k5");
	asm volatile("vfpclasssh $0x12, %xmm1, %k5");
	asm volatile("vfpclasssh $0x12, 0x12345678(%rax,%rcx,8), %k5");
	asm volatile("vfpclasssh $0x12, 0x12345678(%eax,%ecx,8), %k5");
	asm volatile("vgetexpph %zmm2, %zmm1");
	asm volatile("vgetexpph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vgetexpph %xmm2, %xmm1");
	asm volatile("vgetexpph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vgetexpph %ymm2, %ymm1");
	asm volatile("vgetexpph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vgetexpsh %xmm3, %xmm2, %xmm1");
	asm volatile("vgetexpsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vgetexpsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vgetmantph $0x12, %zmm2, %zmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vgetmantph $0x12, %xmm2, %xmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vgetmantph $0x12, %ymm2, %ymm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vgetmantsh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vgetmantsh $0x12, 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vgetmantsh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmaxph %zmm3, %zmm2, %zmm1");
	asm volatile("vmaxph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vmaxph %xmm3, %xmm2, %xmm1");
	asm volatile("vmaxph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmaxph %ymm3, %ymm2, %ymm1");
	asm volatile("vmaxph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vmaxsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmaxsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vmaxsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vminph %zmm3, %zmm2, %zmm1");
	asm volatile("vminph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vminph %xmm3, %xmm2, %xmm1");
	asm volatile("vminph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vminph %ymm3, %ymm2, %ymm1");
	asm volatile("vminph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vminsh %xmm3, %xmm2, %xmm1");
	asm volatile("vminsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vminsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmovsh %xmm1, 0x12345678(%rax,%rcx,8)");
	asm volatile("vmovsh %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vmovsh 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vmovsh 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vmovsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmovw %xmm1, %eax");
	asm volatile("vmovw %xmm1, 0x12345678(%rax,%rcx,8)");
	asm volatile("vmovw %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vmovw %eax, %xmm1");
	asm volatile("vmovw 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vmovw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vmulph %zmm3, %zmm2, %zmm1");
	asm volatile("vmulph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vmulph %xmm3, %xmm2, %xmm1");
	asm volatile("vmulph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmulph %ymm3, %ymm2, %ymm1");
	asm volatile("vmulph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vmulsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmulsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vmulsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrcpph %zmm2, %zmm1");
	asm volatile("vrcpph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrcpph %xmm2, %xmm1");
	asm volatile("vrcpph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrcpph %ymm2, %ymm1");
	asm volatile("vrcpph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrcpsh %xmm3, %xmm2, %xmm1");
	asm volatile("vrcpsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vrcpsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vreduceph $0x12, %zmm2, %zmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vreduceph $0x12, %xmm2, %xmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vreduceph $0x12, %ymm2, %ymm1");
	asm volatile("vreduceph $0x12, 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vreducesh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vreducesh $0x12, 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vreducesh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrndscaleph $0x12, %zmm2, %zmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrndscaleph $0x12, %xmm2, %xmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrndscaleph $0x12, %ymm2, %ymm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrndscalesh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vrndscalesh $0x12, 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vrndscalesh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrsqrtph %zmm2, %zmm1");
	asm volatile("vrsqrtph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrsqrtph %xmm2, %xmm1");
	asm volatile("vrsqrtph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrsqrtph %ymm2, %ymm1");
	asm volatile("vrsqrtph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrsqrtsh %xmm3, %xmm2, %xmm1");
	asm volatile("vrsqrtsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vrsqrtsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vscalefph %zmm3, %zmm2, %zmm1");
	asm volatile("vscalefph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vscalefph %xmm3, %xmm2, %xmm1");
	asm volatile("vscalefph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vscalefph %ymm3, %ymm2, %ymm1");
	asm volatile("vscalefph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vscalefsh %xmm3, %xmm2, %xmm1");
	asm volatile("vscalefsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vscalefsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsqrtph %zmm2, %zmm1");
	asm volatile("vsqrtph 0x12345678(%rax,%rcx,8), %zmm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vsqrtph %xmm2, %xmm1");
	asm volatile("vsqrtph 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vsqrtph %ymm2, %ymm1");
	asm volatile("vsqrtph 0x12345678(%rax,%rcx,8), %ymm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vsqrtsh %xmm3, %xmm2, %xmm1");
	asm volatile("vsqrtsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vsqrtsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsubph %zmm3, %zmm2, %zmm1");
	asm volatile("vsubph 0x12345678(%rax,%rcx,8), %zmm2, %zmm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vsubph %xmm3, %xmm2, %xmm1");
	asm volatile("vsubph 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsubph %ymm3, %ymm2, %ymm1");
	asm volatile("vsubph 0x12345678(%rax,%rcx,8), %ymm2, %ymm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vsubsh %xmm3, %xmm2, %xmm1");
	asm volatile("vsubsh 0x12345678(%rax,%rcx,8), %xmm2, %xmm1");
	asm volatile("vsubsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vucomish %xmm2, %xmm1");
	asm volatile("vucomish 0x12345678(%rax,%rcx,8), %xmm1");
	asm volatile("vucomish 0x12345678(%eax,%ecx,8), %xmm1");

	/* Key Locker */

	asm volatile("loadiwkey %xmm1, %xmm2");
	asm volatile("encodekey128 %eax, %edx");
	asm volatile("encodekey256 %eax, %edx");
	asm volatile("aesenc128kl 0x77(%rdx), %xmm3");
	asm volatile("aesenc256kl 0x77(%rdx), %xmm3");
	asm volatile("aesdec128kl 0x77(%rdx), %xmm3");
	asm volatile("aesdec256kl 0x77(%rdx), %xmm3");
	asm volatile("aesencwide128kl	0x77(%rdx)");
	asm volatile("aesencwide256kl	0x77(%rdx)");
	asm volatile("aesdecwide128kl	0x77(%rdx)");
	asm volatile("aesdecwide256kl	0x77(%rdx)");

	/* Remote Atomic Operations */

	asm volatile("aadd %ecx,(%rax)");
	asm volatile("aadd %edx,(%r8)");
	asm volatile("aadd %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("aadd %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("aadd %rcx,(%rax)");
	asm volatile("aadd %rdx,(%r8)");
	asm volatile("aadd %rdx,(0x12345678)");
	asm volatile("aadd %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("aadd %rdx,0x12345678(%r8,%rcx,8)");

	asm volatile("aand %ecx,(%rax)");
	asm volatile("aand %edx,(%r8)");
	asm volatile("aand %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("aand %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("aand %rcx,(%rax)");
	asm volatile("aand %rdx,(%r8)");
	asm volatile("aand %rdx,(0x12345678)");
	asm volatile("aand %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("aand %rdx,0x12345678(%r8,%rcx,8)");

	asm volatile("aor %ecx,(%rax)");
	asm volatile("aor %edx,(%r8)");
	asm volatile("aor %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("aor %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("aor %rcx,(%rax)");
	asm volatile("aor %rdx,(%r8)");
	asm volatile("aor %rdx,(0x12345678)");
	asm volatile("aor %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("aor %rdx,0x12345678(%r8,%rcx,8)");

	asm volatile("axor %ecx,(%rax)");
	asm volatile("axor %edx,(%r8)");
	asm volatile("axor %edx,0x12345678(%rax,%rcx,8)");
	asm volatile("axor %edx,0x12345678(%r8,%rcx,8)");
	asm volatile("axor %rcx,(%rax)");
	asm volatile("axor %rdx,(%r8)");
	asm volatile("axor %rdx,(0x12345678)");
	asm volatile("axor %rdx,0x12345678(%rax,%rcx,8)");
	asm volatile("axor %rdx,0x12345678(%r8,%rcx,8)");

	/* VEX CMPxxXADD */

	asm volatile("cmpbexadd %ebx,%ecx,(%r9)");
	asm volatile("cmpbxadd %ebx,%ecx,(%r9)");
	asm volatile("cmplexadd %ebx,%ecx,(%r9)");
	asm volatile("cmplxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnbexadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnbxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnlexadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnlxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnoxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnpxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnsxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpnzxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpoxadd %ebx,%ecx,(%r9)");
	asm volatile("cmppxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpsxadd %ebx,%ecx,(%r9)");
	asm volatile("cmpzxadd %ebx,%ecx,(%r9)");

	/* Pre-fetch */

	asm volatile("prefetch (%rax)");
	asm volatile("prefetcht0 (%rax)");
	asm volatile("prefetcht1 (%rax)");
	asm volatile("prefetcht2 (%rax)");
	asm volatile("prefetchnta (%rax)");
	asm volatile("prefetchit0 0x12345678(%rip)");
	asm volatile("prefetchit1 0x12345678(%rip)");

	/* MSR List */

	asm volatile("rdmsrlist");
	asm volatile("wrmsrlist");

	/* User Read/Write MSR */

	asm volatile("urdmsr %rdx,%rax");
	asm volatile("urdmsr %rdx,%r22");
	asm volatile("urdmsr $0x7f,%r12");
	asm volatile("uwrmsr %rax,%rdx");
	asm volatile("uwrmsr %r22,%rdx");
	asm volatile("uwrmsr %r12,$0x7f");

	/* AVX NE Convert */

	asm volatile("vbcstnebf162ps (%rcx),%xmm6");
	asm volatile("vbcstnesh2ps (%rcx),%xmm6");
	asm volatile("vcvtneebf162ps (%rcx),%xmm6");
	asm volatile("vcvtneeph2ps (%rcx),%xmm6");
	asm volatile("vcvtneobf162ps (%rcx),%xmm6");
	asm volatile("vcvtneoph2ps (%rcx),%xmm6");
	asm volatile("vcvtneps2bf16 %xmm1,%xmm6");

	/* FRED */

	asm volatile("erets");	/* Expecting: erets indirect 0 */
	asm volatile("eretu");	/* Expecting: eretu indirect 0 */

	/* AMX Complex */

	asm volatile("tcmmimfp16ps %tmm1,%tmm2,%tmm3");
	asm volatile("tcmmrlfp16ps %tmm1,%tmm2,%tmm3");

	/* AMX FP16 */

	asm volatile("tdpfp16ps %tmm1,%tmm2,%tmm3");

	/* REX2 */

	asm volatile("test $0x5, %r18b");
	asm volatile("test $0x5, %r18d");
	asm volatile("test $0x5, %r18");
	asm volatile("test $0x5, %r18w");
	asm volatile("imull %eax, %r14d");
	asm volatile("imull %eax, %r17d");
	asm volatile("punpckldq (%r18), %mm2");
	asm volatile("leal (%rax), %r16d");
	asm volatile("leal (%rax), %r31d");
	asm volatile("leal (,%r16), %eax");
	asm volatile("leal (,%r31), %eax");
	asm volatile("leal (%r16), %eax");
	asm volatile("leal (%r31), %eax");
	asm volatile("leaq (%rax), %r15");
	asm volatile("leaq (%rax), %r16");
	asm volatile("leaq (%r15), %rax");
	asm volatile("leaq (%r16), %rax");
	asm volatile("leaq (,%r15), %rax");
	asm volatile("leaq (,%r16), %rax");
	asm volatile("add (%r16), %r8");
	asm volatile("add (%r16), %r15");
	asm volatile("mov (,%r9), %r16");
	asm volatile("mov (,%r14), %r16");
	asm volatile("sub (%r10), %r31");
	asm volatile("sub (%r13), %r31");
	asm volatile("leal 1(%r16, %r21), %eax");
	asm volatile("leal 1(%r16, %r26), %r31d");
	asm volatile("leal 129(%r21, %r9), %eax");
	asm volatile("leal 129(%r26, %r9), %r31d");
	/*
	 * Have to use .byte for jmpabs because gas does not support the
	 * mnemonic for some reason, but then it also gets the source line wrong
	 * with .byte, so the following is a workaround.
	 */
	asm volatile(""); /* Expecting: jmp indirect 0 */
	asm volatile(".byte 0xd5, 0x00, 0xa1, 0xef, 0xcd, 0xab, 0x90, 0x78, 0x56, 0x34, 0x12");
	asm volatile("pushp %rbx");
	asm volatile("pushp %r16");
	asm volatile("pushp %r31");
	asm volatile("popp %r31");
	asm volatile("popp %r16");
	asm volatile("popp %rbx");

	/* APX */

	asm volatile("bextr %r25d,%edx,%r10d");
	asm volatile("bextr %r25d,0x123(%r31,%rax,4),%edx");
	asm volatile("bextr %r31,%r15,%r11");
	asm volatile("bextr %r31,0x123(%r31,%rax,4),%r15");
	asm volatile("blsi %r25d,%edx");
	asm volatile("blsi %r31,%r15");
	asm volatile("blsi 0x123(%r31,%rax,4),%r25d");
	asm volatile("blsi 0x123(%r31,%rax,4),%r31");
	asm volatile("blsmsk %r25d,%edx");
	asm volatile("blsmsk %r31,%r15");
	asm volatile("blsmsk 0x123(%r31,%rax,4),%r25d");
	asm volatile("blsmsk 0x123(%r31,%rax,4),%r31");
	asm volatile("blsr %r25d,%edx");
	asm volatile("blsr %r31,%r15");
	asm volatile("blsr 0x123(%r31,%rax,4),%r25d");
	asm volatile("blsr 0x123(%r31,%rax,4),%r31");
	asm volatile("bzhi %r25d,%edx,%r10d");
	asm volatile("bzhi %r25d,0x123(%r31,%rax,4),%edx");
	asm volatile("bzhi %r31,%r15,%r11");
	asm volatile("bzhi %r31,0x123(%r31,%rax,4),%r15");
	asm volatile("cmpbexadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpbexadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpbxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpbxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmplxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmplxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnbexadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnbexadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnbxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnbxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnlexadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnlexadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnlxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnlxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnoxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnoxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnpxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnpxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnsxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnsxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpnzxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpnzxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpoxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpoxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmppxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmppxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpsxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpsxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("cmpzxadd %r25d,%edx,0x123(%r31,%rax,4)");
	asm volatile("cmpzxadd %r31,%r15,0x123(%r31,%rax,4)");
	asm volatile("crc32q %r31, %r22");
	asm volatile("crc32q (%r31), %r22");
	asm volatile("crc32b %r19b, %r17");
	asm volatile("crc32b %r19b, %r21d");
	asm volatile("crc32b (%r19),%ebx");
	asm volatile("crc32l %r31d, %r23d");
	asm volatile("crc32l (%r31), %r23d");
	asm volatile("crc32w %r31w, %r21d");
	asm volatile("crc32w (%r31),%r21d");
	asm volatile("crc32 %rax, %r18");
	asm volatile("enqcmd 0x123(%r31d,%eax,4),%r25d");
	asm volatile("enqcmd 0x123(%r31,%rax,4),%r31");
	asm volatile("enqcmds 0x123(%r31d,%eax,4),%r25d");
	asm volatile("enqcmds 0x123(%r31,%rax,4),%r31");
	asm volatile("invept 0x123(%r31,%rax,4),%r31");
	asm volatile("invpcid 0x123(%r31,%rax,4),%r31");
	asm volatile("invvpid 0x123(%r31,%rax,4),%r31");
	asm volatile("kmovb %k5,%r25d");
	asm volatile("kmovb %k5,0x123(%r31,%rax,4)");
	asm volatile("kmovb %r25d,%k5");
	asm volatile("kmovb 0x123(%r31,%rax,4),%k5");
	asm volatile("kmovd %k5,%r25d");
	asm volatile("kmovd %k5,0x123(%r31,%rax,4)");
	asm volatile("kmovd %r25d,%k5");
	asm volatile("kmovd 0x123(%r31,%rax,4),%k5");
	asm volatile("kmovq %k5,%r31");
	asm volatile("kmovq %k5,0x123(%r31,%rax,4)");
	asm volatile("kmovq %r31,%k5");
	asm volatile("kmovq 0x123(%r31,%rax,4),%k5");
	asm volatile("kmovw %k5,%r25d");
	asm volatile("kmovw %k5,0x123(%r31,%rax,4)");
	asm volatile("kmovw %r25d,%k5");
	asm volatile("kmovw 0x123(%r31,%rax,4),%k5");
	asm volatile("ldtilecfg 0x123(%r31,%rax,4)");
	asm volatile("movbe %r18w,%ax");
	asm volatile("movbe %r15w,%ax");
	asm volatile("movbe %r18w,0x123(%r16,%rax,4)");
	asm volatile("movbe %r18w,0x123(%r31,%rax,4)");
	asm volatile("movbe %r25d,%edx");
	asm volatile("movbe %r15d,%edx");
	asm volatile("movbe %r25d,0x123(%r16,%rax,4)");
	asm volatile("movbe %r31,%r15");
	asm volatile("movbe %r8,%r15");
	asm volatile("movbe %r31,0x123(%r16,%rax,4)");
	asm volatile("movbe %r31,0x123(%r31,%rax,4)");
	asm volatile("movbe 0x123(%r16,%rax,4),%r31");
	asm volatile("movbe 0x123(%r31,%rax,4),%r18w");
	asm volatile("movbe 0x123(%r31,%rax,4),%r25d");
	asm volatile("movdir64b 0x123(%r31d,%eax,4),%r25d");
	asm volatile("movdir64b 0x123(%r31,%rax,4),%r31");
	asm volatile("movdiri %r25d,0x123(%r31,%rax,4)");
	asm volatile("movdiri %r31,0x123(%r31,%rax,4)");
	asm volatile("pdep %r25d,%edx,%r10d");
	asm volatile("pdep %r31,%r15,%r11");
	asm volatile("pdep 0x123(%r31,%rax,4),%r25d,%edx");
	asm volatile("pdep 0x123(%r31,%rax,4),%r31,%r15");
	asm volatile("pext %r25d,%edx,%r10d");
	asm volatile("pext %r31,%r15,%r11");
	asm volatile("pext 0x123(%r31,%rax,4),%r25d,%edx");
	asm volatile("pext 0x123(%r31,%rax,4),%r31,%r15");
	asm volatile("shlx %r25d,%edx,%r10d");
	asm volatile("shlx %r25d,0x123(%r31,%rax,4),%edx");
	asm volatile("shlx %r31,%r15,%r11");
	asm volatile("shlx %r31,0x123(%r31,%rax,4),%r15");
	asm volatile("shrx %r25d,%edx,%r10d");
	asm volatile("shrx %r25d,0x123(%r31,%rax,4),%edx");
	asm volatile("shrx %r31,%r15,%r11");
	asm volatile("shrx %r31,0x123(%r31,%rax,4),%r15");
	asm volatile("sttilecfg 0x123(%r31,%rax,4)");
	asm volatile("tileloadd 0x123(%r31,%rax,4),%tmm6");
	asm volatile("tileloaddt1 0x123(%r31,%rax,4),%tmm6");
	asm volatile("tilestored %tmm6,0x123(%r31,%rax,4)");
	asm volatile("vbroadcastf128 (%r16),%ymm3");
	asm volatile("vbroadcasti128 (%r16),%ymm3");
	asm volatile("vextractf128 $1,%ymm3,(%r16)");
	asm volatile("vextracti128 $1,%ymm3,(%r16)");
	asm volatile("vinsertf128 $1,(%r16),%ymm3,%ymm8");
	asm volatile("vinserti128 $1,(%r16),%ymm3,%ymm8");
	asm volatile("vroundpd $1,(%r24),%xmm6");
	asm volatile("vroundps $2,(%r24),%xmm6");
	asm volatile("vroundsd $3,(%r24),%xmm6,%xmm3");
	asm volatile("vroundss $4,(%r24),%xmm6,%xmm3");
	asm volatile("wrssd %r25d,0x123(%r31,%rax,4)");
	asm volatile("wrssq %r31,0x123(%r31,%rax,4)");
	asm volatile("wrussd %r25d,0x123(%r31,%rax,4)");
	asm volatile("wrussq %r31,0x123(%r31,%rax,4)");

	/* APX new data destination */

	asm volatile("adc $0x1234,%ax,%r30w");
	asm volatile("adc %r15b,%r17b,%r18b");
	asm volatile("adc %r15d,(%r8),%r18d");
	asm volatile("adc (%r15,%rax,1),%r16b,%r8b");
	asm volatile("adc (%r15,%rax,1),%r16w,%r8w");
	asm volatile("adcl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("adcx %r15d,%r8d,%r18d");
	asm volatile("adcx (%r15,%r31,1),%r8");
	asm volatile("adcx (%r15,%r31,1),%r8d,%r18d");
	asm volatile("add $0x1234,%ax,%r30w");
	asm volatile("add $0x12344433,%r15,%r16");
	asm volatile("add $0x34,%r13b,%r17b");
	asm volatile("add $0xfffffffff4332211,%rax,%r8");
	asm volatile("add %r31,%r8,%r16");
	asm volatile("add %r31,(%r8),%r16");
	asm volatile("add %r31,(%r8,%r16,8),%r16");
	asm volatile("add %r31b,%r8b,%r16b");
	asm volatile("add %r31d,%r8d,%r16d");
	asm volatile("add %r31w,%r8w,%r16w");
	asm volatile("add (%r31),%r8,%r16");
	asm volatile("add 0x9090(%r31,%r16,1),%r8,%r16");
	asm volatile("addb %r31b,%r8b,%r16b");
	asm volatile("addl %r31d,%r8d,%r16d");
	asm volatile("addl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("addq %r31,%r8,%r16");
	asm volatile("addq $0x12344433,(%r15,%rcx,4),%r16");
	asm volatile("addw %r31w,%r8w,%r16w");
	asm volatile("adox %r15d,%r8d,%r18d");
	asm volatile("{load} add %r31,%r8,%r16");
	asm volatile("{store} add %r31,%r8,%r16");
	asm volatile("adox (%r15,%r31,1),%r8");
	asm volatile("adox (%r15,%r31,1),%r8d,%r18d");
	asm volatile("and $0x1234,%ax,%r30w");
	asm volatile("and %r15b,%r17b,%r18b");
	asm volatile("and %r15d,(%r8),%r18d");
	asm volatile("and (%r15,%rax,1),%r16b,%r8b");
	asm volatile("and (%r15,%rax,1),%r16w,%r8w");
	asm volatile("andl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("cmova 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovae 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovb 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovbe 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmove 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovg 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovge 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovl 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovle 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovne 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovno 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovnp 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovns 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovo 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovp 0x90909090(%eax),%edx,%r8d");
	asm volatile("cmovs 0x90909090(%eax),%edx,%r8d");
	asm volatile("dec %rax,%r17");
	asm volatile("decb (%r31,%r12,1),%r8b");
	asm volatile("imul 0x909(%rax,%r31,8),%rdx,%r25");
	asm volatile("imul 0x90909(%eax),%edx,%r8d");
	asm volatile("inc %r31,%r16");
	asm volatile("inc %r31,%r8");
	asm volatile("inc %rax,%rbx");
	asm volatile("neg %rax,%r17");
	asm volatile("negb (%r31,%r12,1),%r8b");
	asm volatile("not %rax,%r17");
	asm volatile("notb (%r31,%r12,1),%r8b");
	asm volatile("or $0x1234,%ax,%r30w");
	asm volatile("or %r15b,%r17b,%r18b");
	asm volatile("or %r15d,(%r8),%r18d");
	asm volatile("or (%r15,%rax,1),%r16b,%r8b");
	asm volatile("or (%r15,%rax,1),%r16w,%r8w");
	asm volatile("orl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("rcl $0x2,%r12b,%r31b");
	asm volatile("rcl %cl,%r16b,%r8b");
	asm volatile("rclb $0x1,(%rax),%r31b");
	asm volatile("rcll $0x2,(%rax),%r31d");
	asm volatile("rclw $0x1,(%rax),%r31w");
	asm volatile("rclw %cl,(%r19,%rax,4),%r31w");
	asm volatile("rcr $0x2,%r12b,%r31b");
	asm volatile("rcr %cl,%r16b,%r8b");
	asm volatile("rcrb $0x1,(%rax),%r31b");
	asm volatile("rcrl $0x2,(%rax),%r31d");
	asm volatile("rcrw $0x1,(%rax),%r31w");
	asm volatile("rcrw %cl,(%r19,%rax,4),%r31w");
	asm volatile("rol $0x2,%r12b,%r31b");
	asm volatile("rol %cl,%r16b,%r8b");
	asm volatile("rolb $0x1,(%rax),%r31b");
	asm volatile("roll $0x2,(%rax),%r31d");
	asm volatile("rolw $0x1,(%rax),%r31w");
	asm volatile("rolw %cl,(%r19,%rax,4),%r31w");
	asm volatile("ror $0x2,%r12b,%r31b");
	asm volatile("ror %cl,%r16b,%r8b");
	asm volatile("rorb $0x1,(%rax),%r31b");
	asm volatile("rorl $0x2,(%rax),%r31d");
	asm volatile("rorw $0x1,(%rax),%r31w");
	asm volatile("rorw %cl,(%r19,%rax,4),%r31w");
	asm volatile("sar $0x2,%r12b,%r31b");
	asm volatile("sar %cl,%r16b,%r8b");
	asm volatile("sarb $0x1,(%rax),%r31b");
	asm volatile("sarl $0x2,(%rax),%r31d");
	asm volatile("sarw $0x1,(%rax),%r31w");
	asm volatile("sarw %cl,(%r19,%rax,4),%r31w");
	asm volatile("sbb $0x1234,%ax,%r30w");
	asm volatile("sbb %r15b,%r17b,%r18b");
	asm volatile("sbb %r15d,(%r8),%r18d");
	asm volatile("sbb (%r15,%rax,1),%r16b,%r8b");
	asm volatile("sbb (%r15,%rax,1),%r16w,%r8w");
	asm volatile("sbbl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("shl $0x2,%r12b,%r31b");
	asm volatile("shl $0x2,%r12b,%r31b");
	asm volatile("shl %cl,%r16b,%r8b");
	asm volatile("shl %cl,%r16b,%r8b");
	asm volatile("shlb $0x1,(%rax),%r31b");
	asm volatile("shlb $0x1,(%rax),%r31b");
	asm volatile("shld $0x1,%r12,(%rax),%r31");
	asm volatile("shld $0x2,%r15d,(%rax),%r31d");
	asm volatile("shld $0x2,%r8w,%r12w,%r31w");
	asm volatile("shld %cl,%r12,%r16,%r8");
	asm volatile("shld %cl,%r13w,(%r19,%rax,4),%r31w");
	asm volatile("shld %cl,%r9w,(%rax),%r31w");
	asm volatile("shll $0x2,(%rax),%r31d");
	asm volatile("shll $0x2,(%rax),%r31d");
	asm volatile("shlw $0x1,(%rax),%r31w");
	asm volatile("shlw $0x1,(%rax),%r31w");
	asm volatile("shlw %cl,(%r19,%rax,4),%r31w");
	asm volatile("shlw %cl,(%r19,%rax,4),%r31w");
	asm volatile("shr $0x2,%r12b,%r31b");
	asm volatile("shr %cl,%r16b,%r8b");
	asm volatile("shrb $0x1,(%rax),%r31b");
	asm volatile("shrd $0x1,%r12,(%rax),%r31");
	asm volatile("shrd $0x2,%r15d,(%rax),%r31d");
	asm volatile("shrd $0x2,%r8w,%r12w,%r31w");
	asm volatile("shrd %cl,%r12,%r16,%r8");
	asm volatile("shrd %cl,%r13w,(%r19,%rax,4),%r31w");
	asm volatile("shrd %cl,%r9w,(%rax),%r31w");
	asm volatile("shrl $0x2,(%rax),%r31d");
	asm volatile("shrw $0x1,(%rax),%r31w");
	asm volatile("shrw %cl,(%r19,%rax,4),%r31w");
	asm volatile("sub $0x1234,%ax,%r30w");
	asm volatile("sub %r15b,%r17b,%r18b");
	asm volatile("sub %r15d,(%r8),%r18d");
	asm volatile("sub (%r15,%rax,1),%r16b,%r8b");
	asm volatile("sub (%r15,%rax,1),%r16w,%r8w");
	asm volatile("subl $0x11,(%r19,%rax,4),%r20d");
	asm volatile("xor $0x1234,%ax,%r30w");
	asm volatile("xor %r15b,%r17b,%r18b");
	asm volatile("xor %r15d,(%r8),%r18d");
	asm volatile("xor (%r15,%rax,1),%r16b,%r8b");
	asm volatile("xor (%r15,%rax,1),%r16w,%r8w");
	asm volatile("xorl $0x11,(%r19,%rax,4),%r20d");

	/* APX suppress status flags */

	asm volatile("{nf} add %bl,%dl,%r8b");
	asm volatile("{nf} add %dx,%ax,%r9w");
	asm volatile("{nf} add 0x123(%r8,%rax,4),%bl,%dl");
	asm volatile("{nf} add 0x123(%r8,%rax,4),%dx,%ax");
	asm volatile("{nf} or %bl,%dl,%r8b");
	asm volatile("{nf} or %dx,%ax,%r9w");
	asm volatile("{nf} or 0x123(%r8,%rax,4),%bl,%dl");
	asm volatile("{nf} or 0x123(%r8,%rax,4),%dx,%ax");
	asm volatile("{nf} and %bl,%dl,%r8b");
	asm volatile("{nf} and %dx,%ax,%r9w");
	asm volatile("{nf} and 0x123(%r8,%rax,4),%bl,%dl");
	asm volatile("{nf} and 0x123(%r8,%rax,4),%dx,%ax");
	asm volatile("{nf} shld $0x7b,%dx,%ax,%r9w");
	asm volatile("{nf} sub %bl,%dl,%r8b");
	asm volatile("{nf} sub %dx,%ax,%r9w");
	asm volatile("{nf} sub 0x123(%r8,%rax,4),%bl,%dl");
	asm volatile("{nf} sub 0x123(%r8,%rax,4),%dx,%ax");
	asm volatile("{nf} shrd $0x7b,%dx,%ax,%r9w");
	asm volatile("{nf} xor %bl,%dl,%r8b");
	asm volatile("{nf} xor %r31,%r31");
	asm volatile("{nf} xor 0x123(%r8,%rax,4),%bl,%dl");
	asm volatile("{nf} xor 0x123(%r8,%rax,4),%dx,%ax");
	asm volatile("{nf} imul $0xff90,%r9,%r15");
	asm volatile("{nf} imul $0x7b,%r9,%r15");
	asm volatile("{nf} xor $0x7b,%bl,%dl");
	asm volatile("{nf} xor $0x7b,%dx,%ax");
	asm volatile("{nf} popcnt %r9,%r31");
	asm volatile("{nf} shld %cl,%dx,%ax,%r9w");
	asm volatile("{nf} shrd %cl,%dx,%ax,%r9w");
	asm volatile("{nf} imul %r9,%r31,%r11");
	asm volatile("{nf} sar $0x7b,%bl,%dl");
	asm volatile("{nf} sar $0x7b,%dx,%ax");
	asm volatile("{nf} sar $1,%bl,%dl");
	asm volatile("{nf} sar $1,%dx,%ax");
	asm volatile("{nf} sar %cl,%bl,%dl");
	asm volatile("{nf} sar %cl,%dx,%ax");
	asm volatile("{nf} andn %r9,%r31,%r11");
	asm volatile("{nf} blsi %r9,%r31");
	asm volatile("{nf} tzcnt %r9,%r31");
	asm volatile("{nf} lzcnt %r9,%r31");
	asm volatile("{nf} idiv %bl");
	asm volatile("{nf} idiv %dx");
	asm volatile("{nf} dec %bl,%dl");
	asm volatile("{nf} dec %dx,%ax");

#else  /* #ifdef __x86_64__ */

	/* bound r32, mem (same op code as EVEX prefix) */

	asm volatile("bound %eax, 0x12345678(%ecx)");
	asm volatile("bound %ecx, 0x12345678(%eax)");
	asm volatile("bound %edx, 0x12345678(%eax)");
	asm volatile("bound %ebx, 0x12345678(%eax)");
	asm volatile("bound %esp, 0x12345678(%eax)");
	asm volatile("bound %ebp, 0x12345678(%eax)");
	asm volatile("bound %esi, 0x12345678(%eax)");
	asm volatile("bound %edi, 0x12345678(%eax)");
	asm volatile("bound %ecx, (%eax)");
	asm volatile("bound %eax, (0x12345678)");
	asm volatile("bound %edx, (%ecx,%eax,1)");
	asm volatile("bound %edx, 0x12345678(,%eax,1)");
	asm volatile("bound %edx, (%eax,%ecx,1)");
	asm volatile("bound %edx, (%eax,%ecx,8)");
	asm volatile("bound %edx, 0x12(%eax)");
	asm volatile("bound %edx, 0x12(%ebp)");
	asm volatile("bound %edx, 0x12(%ecx,%eax,1)");
	asm volatile("bound %edx, 0x12(%ebp,%eax,1)");
	asm volatile("bound %edx, 0x12(%eax,%ecx,1)");
	asm volatile("bound %edx, 0x12(%eax,%ecx,8)");
	asm volatile("bound %edx, 0x12345678(%eax)");
	asm volatile("bound %edx, 0x12345678(%ebp)");
	asm volatile("bound %edx, 0x12345678(%ecx,%eax,1)");
	asm volatile("bound %edx, 0x12345678(%ebp,%eax,1)");
	asm volatile("bound %edx, 0x12345678(%eax,%ecx,1)");
	asm volatile("bound %edx, 0x12345678(%eax,%ecx,8)");

	/* bound r16, mem (same op code as EVEX prefix) */

	asm volatile("bound %ax, 0x12345678(%ecx)");
	asm volatile("bound %cx, 0x12345678(%eax)");
	asm volatile("bound %dx, 0x12345678(%eax)");
	asm volatile("bound %bx, 0x12345678(%eax)");
	asm volatile("bound %sp, 0x12345678(%eax)");
	asm volatile("bound %bp, 0x12345678(%eax)");
	asm volatile("bound %si, 0x12345678(%eax)");
	asm volatile("bound %di, 0x12345678(%eax)");
	asm volatile("bound %cx, (%eax)");
	asm volatile("bound %ax, (0x12345678)");
	asm volatile("bound %dx, (%ecx,%eax,1)");
	asm volatile("bound %dx, 0x12345678(,%eax,1)");
	asm volatile("bound %dx, (%eax,%ecx,1)");
	asm volatile("bound %dx, (%eax,%ecx,8)");
	asm volatile("bound %dx, 0x12(%eax)");
	asm volatile("bound %dx, 0x12(%ebp)");
	asm volatile("bound %dx, 0x12(%ecx,%eax,1)");
	asm volatile("bound %dx, 0x12(%ebp,%eax,1)");
	asm volatile("bound %dx, 0x12(%eax,%ecx,1)");
	asm volatile("bound %dx, 0x12(%eax,%ecx,8)");
	asm volatile("bound %dx, 0x12345678(%eax)");
	asm volatile("bound %dx, 0x12345678(%ebp)");
	asm volatile("bound %dx, 0x12345678(%ecx,%eax,1)");
	asm volatile("bound %dx, 0x12345678(%ebp,%eax,1)");
	asm volatile("bound %dx, 0x12345678(%eax,%ecx,1)");
	asm volatile("bound %dx, 0x12345678(%eax,%ecx,8)");

	/* AVX-512: Instructions with the same op codes as Mask Instructions  */

	asm volatile("cmovno %eax,%ebx");
	asm volatile("cmovno 0x12345678(%eax),%ecx");
	asm volatile("cmovno 0x12345678(%eax),%cx");

	asm volatile("cmove  %eax,%ebx");
	asm volatile("cmove 0x12345678(%eax),%ecx");
	asm volatile("cmove 0x12345678(%eax),%cx");

	asm volatile("seto    0x12345678(%eax)");
	asm volatile("setno   0x12345678(%eax)");
	asm volatile("setb    0x12345678(%eax)");
	asm volatile("setc    0x12345678(%eax)");
	asm volatile("setnae  0x12345678(%eax)");
	asm volatile("setae   0x12345678(%eax)");
	asm volatile("setnb   0x12345678(%eax)");
	asm volatile("setnc   0x12345678(%eax)");
	asm volatile("sets    0x12345678(%eax)");
	asm volatile("setns   0x12345678(%eax)");

	/* AVX-512: Mask Instructions */

	asm volatile("kandw  %k7,%k6,%k5");
	asm volatile("kandq  %k7,%k6,%k5");
	asm volatile("kandb  %k7,%k6,%k5");
	asm volatile("kandd  %k7,%k6,%k5");

	asm volatile("kandnw  %k7,%k6,%k5");
	asm volatile("kandnq  %k7,%k6,%k5");
	asm volatile("kandnb  %k7,%k6,%k5");
	asm volatile("kandnd  %k7,%k6,%k5");

	asm volatile("knotw  %k7,%k6");
	asm volatile("knotq  %k7,%k6");
	asm volatile("knotb  %k7,%k6");
	asm volatile("knotd  %k7,%k6");

	asm volatile("korw  %k7,%k6,%k5");
	asm volatile("korq  %k7,%k6,%k5");
	asm volatile("korb  %k7,%k6,%k5");
	asm volatile("kord  %k7,%k6,%k5");

	asm volatile("kxnorw  %k7,%k6,%k5");
	asm volatile("kxnorq  %k7,%k6,%k5");
	asm volatile("kxnorb  %k7,%k6,%k5");
	asm volatile("kxnord  %k7,%k6,%k5");

	asm volatile("kxorw  %k7,%k6,%k5");
	asm volatile("kxorq  %k7,%k6,%k5");
	asm volatile("kxorb  %k7,%k6,%k5");
	asm volatile("kxord  %k7,%k6,%k5");

	asm volatile("kaddw  %k7,%k6,%k5");
	asm volatile("kaddq  %k7,%k6,%k5");
	asm volatile("kaddb  %k7,%k6,%k5");
	asm volatile("kaddd  %k7,%k6,%k5");

	asm volatile("kunpckbw %k7,%k6,%k5");
	asm volatile("kunpckwd %k7,%k6,%k5");
	asm volatile("kunpckdq %k7,%k6,%k5");

	asm volatile("kmovw  %k6,%k5");
	asm volatile("kmovw  (%ecx),%k5");
	asm volatile("kmovw  0x123(%eax,%ecx,8),%k5");
	asm volatile("kmovw  %k5,(%ecx)");
	asm volatile("kmovw  %k5,0x123(%eax,%ecx,8)");
	asm volatile("kmovw  %eax,%k5");
	asm volatile("kmovw  %ebp,%k5");
	asm volatile("kmovw  %k5,%eax");
	asm volatile("kmovw  %k5,%ebp");

	asm volatile("kmovq  %k6,%k5");
	asm volatile("kmovq  (%ecx),%k5");
	asm volatile("kmovq  0x123(%eax,%ecx,8),%k5");
	asm volatile("kmovq  %k5,(%ecx)");
	asm volatile("kmovq  %k5,0x123(%eax,%ecx,8)");

	asm volatile("kmovb  %k6,%k5");
	asm volatile("kmovb  (%ecx),%k5");
	asm volatile("kmovb  0x123(%eax,%ecx,8),%k5");
	asm volatile("kmovb  %k5,(%ecx)");
	asm volatile("kmovb  %k5,0x123(%eax,%ecx,8)");
	asm volatile("kmovb  %eax,%k5");
	asm volatile("kmovb  %ebp,%k5");
	asm volatile("kmovb  %k5,%eax");
	asm volatile("kmovb  %k5,%ebp");

	asm volatile("kmovd  %k6,%k5");
	asm volatile("kmovd  (%ecx),%k5");
	asm volatile("kmovd  0x123(%eax,%ecx,8),%k5");
	asm volatile("kmovd  %k5,(%ecx)");
	asm volatile("kmovd  %k5,0x123(%eax,%ecx,8)");
	asm volatile("kmovd  %eax,%k5");
	asm volatile("kmovd  %ebp,%k5");
	asm volatile("kmovd  %k5,%eax");
	asm volatile("kmovd  %k5,%ebp");

	asm volatile("kortestw %k6,%k5");
	asm volatile("kortestq %k6,%k5");
	asm volatile("kortestb %k6,%k5");
	asm volatile("kortestd %k6,%k5");

	asm volatile("ktestw %k6,%k5");
	asm volatile("ktestq %k6,%k5");
	asm volatile("ktestb %k6,%k5");
	asm volatile("ktestd %k6,%k5");

	asm volatile("kshiftrw $0x12,%k6,%k5");
	asm volatile("kshiftrq $0x5b,%k6,%k5");
	asm volatile("kshiftlw $0x12,%k6,%k5");
	asm volatile("kshiftlq $0x5b,%k6,%k5");

	/* AVX-512: Op code 0f 5b */
	asm volatile("vcvtdq2ps %xmm5,%xmm6");
	asm volatile("vcvtqq2ps %zmm5,%ymm6{%k7}");
	asm volatile("vcvtps2dq %xmm5,%xmm6");
	asm volatile("vcvttps2dq %xmm5,%xmm6");

	/* AVX-512: Op code 0f 6f */

	asm volatile("movq   %mm0,%mm4");
	asm volatile("vmovdqa %ymm4,%ymm6");
	asm volatile("vmovdqa32 %zmm5,%zmm6");
	asm volatile("vmovdqa64 %zmm5,%zmm6");
	asm volatile("vmovdqu %ymm4,%ymm6");
	asm volatile("vmovdqu32 %zmm5,%zmm6");
	asm volatile("vmovdqu64 %zmm5,%zmm6");
	asm volatile("vmovdqu8 %zmm5,%zmm6");
	asm volatile("vmovdqu16 %zmm5,%zmm6");

	/* AVX-512: Op code 0f 78 */

	asm volatile("vmread %eax,%ebx");
	asm volatile("vcvttps2udq %zmm5,%zmm6");
	asm volatile("vcvttpd2udq %zmm5,%ymm6{%k7}");
	asm volatile("vcvttsd2usi %xmm6,%eax");
	asm volatile("vcvttss2usi %xmm6,%eax");
	asm volatile("vcvttps2uqq %ymm5,%zmm6{%k7}");
	asm volatile("vcvttpd2uqq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 79 */

	asm volatile("vmwrite %eax,%ebx");
	asm volatile("vcvtps2udq %zmm5,%zmm6");
	asm volatile("vcvtpd2udq %zmm5,%ymm6{%k7}");
	asm volatile("vcvtsd2usi %xmm6,%eax");
	asm volatile("vcvtss2usi %xmm6,%eax");
	asm volatile("vcvtps2uqq %ymm5,%zmm6{%k7}");
	asm volatile("vcvtpd2uqq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 7a */

	asm volatile("vcvtudq2pd %ymm5,%zmm6{%k7}");
	asm volatile("vcvtuqq2pd %zmm5,%zmm6");
	asm volatile("vcvtudq2ps %zmm5,%zmm6");
	asm volatile("vcvtuqq2ps %zmm5,%ymm6{%k7}");
	asm volatile("vcvttps2qq %ymm5,%zmm6{%k7}");
	asm volatile("vcvttpd2qq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 7b */

	asm volatile("vcvtusi2sd %eax,%xmm5,%xmm6");
	asm volatile("vcvtusi2ss %eax,%xmm5,%xmm6");
	asm volatile("vcvtps2qq %ymm5,%zmm6{%k7}");
	asm volatile("vcvtpd2qq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 7f */

	asm volatile("movq.s  %mm0,%mm4");
	asm volatile("vmovdqa.s %ymm5,%ymm6");
	asm volatile("vmovdqa32.s %zmm5,%zmm6");
	asm volatile("vmovdqa64.s %zmm5,%zmm6");
	asm volatile("vmovdqu.s %ymm5,%ymm6");
	asm volatile("vmovdqu32.s %zmm5,%zmm6");
	asm volatile("vmovdqu64.s %zmm5,%zmm6");
	asm volatile("vmovdqu8.s %zmm5,%zmm6");
	asm volatile("vmovdqu16.s %zmm5,%zmm6");

	/* AVX-512: Op code 0f db */

	asm volatile("pand  %mm1,%mm2");
	asm volatile("pand  %xmm1,%xmm2");
	asm volatile("vpand  %ymm4,%ymm6,%ymm2");
	asm volatile("vpandd %zmm4,%zmm5,%zmm6");
	asm volatile("vpandq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f df */

	asm volatile("pandn  %mm1,%mm2");
	asm volatile("pandn  %xmm1,%xmm2");
	asm volatile("vpandn %ymm4,%ymm6,%ymm2");
	asm volatile("vpandnd %zmm4,%zmm5,%zmm6");
	asm volatile("vpandnq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f e6 */

	asm volatile("vcvttpd2dq %xmm1,%xmm2");
	asm volatile("vcvtdq2pd %xmm5,%xmm6");
	asm volatile("vcvtdq2pd %ymm5,%zmm6{%k7}");
	asm volatile("vcvtqq2pd %zmm5,%zmm6");
	asm volatile("vcvtpd2dq %xmm1,%xmm2");

	/* AVX-512: Op code 0f eb */

	asm volatile("por   %mm4,%mm6");
	asm volatile("vpor   %ymm4,%ymm6,%ymm2");
	asm volatile("vpord  %zmm4,%zmm5,%zmm6");
	asm volatile("vporq  %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f ef */

	asm volatile("pxor   %mm4,%mm6");
	asm volatile("vpxor  %ymm4,%ymm6,%ymm2");
	asm volatile("vpxord %zmm4,%zmm5,%zmm6");
	asm volatile("vpxorq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 10 */

	asm volatile("pblendvb %xmm1,%xmm0");
	asm volatile("vpsrlvw %zmm4,%zmm5,%zmm6");
	asm volatile("vpmovuswb %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 11 */

	asm volatile("vpmovusdb %zmm5,%xmm6{%k7}");
	asm volatile("vpsravw %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 12 */

	asm volatile("vpmovusqb %zmm5,%xmm6{%k7}");
	asm volatile("vpsllvw %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 13 */

	asm volatile("vcvtph2ps %xmm3,%ymm5");
	asm volatile("vcvtph2ps %ymm5,%zmm6{%k7}");
	asm volatile("vpmovusdw %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 14 */

	asm volatile("blendvps %xmm1,%xmm0");
	asm volatile("vpmovusqw %zmm5,%xmm6{%k7}");
	asm volatile("vprorvd %zmm4,%zmm5,%zmm6");
	asm volatile("vprorvq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 15 */

	asm volatile("blendvpd %xmm1,%xmm0");
	asm volatile("vpmovusqd %zmm5,%ymm6{%k7}");
	asm volatile("vprolvd %zmm4,%zmm5,%zmm6");
	asm volatile("vprolvq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 16 */

	asm volatile("vpermps %ymm4,%ymm6,%ymm2");
	asm volatile("vpermps %ymm4,%ymm6,%ymm2{%k7}");
	asm volatile("vpermpd %ymm4,%ymm6,%ymm2{%k7}");

	/* AVX-512: Op code 0f 38 19 */

	asm volatile("vbroadcastsd %xmm4,%ymm6");
	asm volatile("vbroadcastf32x2 %xmm7,%zmm6");

	/* AVX-512: Op code 0f 38 1a */

	asm volatile("vbroadcastf128 (%ecx),%ymm4");
	asm volatile("vbroadcastf32x4 (%ecx),%zmm6");
	asm volatile("vbroadcastf64x2 (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 1b */

	asm volatile("vbroadcastf32x8 (%ecx),%zmm6");
	asm volatile("vbroadcastf64x4 (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 1f */

	asm volatile("vpabsq %zmm4,%zmm6");

	/* AVX-512: Op code 0f 38 20 */

	asm volatile("vpmovsxbw %xmm4,%xmm5");
	asm volatile("vpmovswb %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 21 */

	asm volatile("vpmovsxbd %xmm4,%ymm6");
	asm volatile("vpmovsdb %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 22 */

	asm volatile("vpmovsxbq %xmm4,%ymm4");
	asm volatile("vpmovsqb %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 23 */

	asm volatile("vpmovsxwd %xmm4,%ymm4");
	asm volatile("vpmovsdw %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 24 */

	asm volatile("vpmovsxwq %xmm4,%ymm6");
	asm volatile("vpmovsqw %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 25 */

	asm volatile("vpmovsxdq %xmm4,%ymm4");
	asm volatile("vpmovsqd %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 26 */

	asm volatile("vptestmb %zmm5,%zmm6,%k5");
	asm volatile("vptestmw %zmm5,%zmm6,%k5");
	asm volatile("vptestnmb %zmm4,%zmm5,%k5");
	asm volatile("vptestnmw %zmm4,%zmm5,%k5");

	/* AVX-512: Op code 0f 38 27 */

	asm volatile("vptestmd %zmm5,%zmm6,%k5");
	asm volatile("vptestmq %zmm5,%zmm6,%k5");
	asm volatile("vptestnmd %zmm4,%zmm5,%k5");
	asm volatile("vptestnmq %zmm4,%zmm5,%k5");

	/* AVX-512: Op code 0f 38 28 */

	asm volatile("vpmuldq %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovm2b %k5,%zmm6");
	asm volatile("vpmovm2w %k5,%zmm6");

	/* AVX-512: Op code 0f 38 29 */

	asm volatile("vpcmpeqq %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovb2m %zmm6,%k5");
	asm volatile("vpmovw2m %zmm6,%k5");

	/* AVX-512: Op code 0f 38 2a */

	asm volatile("vmovntdqa (%ecx),%ymm4");
	asm volatile("vpbroadcastmb2q %k6,%zmm1");

	/* AVX-512: Op code 0f 38 2c */

	asm volatile("vmaskmovps (%ecx),%ymm4,%ymm6");
	asm volatile("vscalefps %zmm4,%zmm5,%zmm6");
	asm volatile("vscalefpd %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 2d */

	asm volatile("vmaskmovpd (%ecx),%ymm4,%ymm6");
	asm volatile("vscalefss %xmm4,%xmm5,%xmm6{%k7}");
	asm volatile("vscalefsd %xmm4,%xmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 30 */

	asm volatile("vpmovzxbw %xmm4,%ymm4");
	asm volatile("vpmovwb %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 31 */

	asm volatile("vpmovzxbd %xmm4,%ymm6");
	asm volatile("vpmovdb %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 32 */

	asm volatile("vpmovzxbq %xmm4,%ymm4");
	asm volatile("vpmovqb %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 33 */

	asm volatile("vpmovzxwd %xmm4,%ymm4");
	asm volatile("vpmovdw %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 34 */

	asm volatile("vpmovzxwq %xmm4,%ymm6");
	asm volatile("vpmovqw %zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 35 */

	asm volatile("vpmovzxdq %xmm4,%ymm4");
	asm volatile("vpmovqd %zmm5,%ymm6{%k7}");

	/* AVX-512: Op code 0f 38 36 */

	asm volatile("vpermd %ymm4,%ymm6,%ymm2");
	asm volatile("vpermd %ymm4,%ymm6,%ymm2{%k7}");
	asm volatile("vpermq %ymm4,%ymm6,%ymm2{%k7}");

	/* AVX-512: Op code 0f 38 38 */

	asm volatile("vpminsb %ymm4,%ymm6,%ymm2");
	asm volatile("vpmovm2d %k5,%zmm6");
	asm volatile("vpmovm2q %k5,%zmm6");

	/* AVX-512: Op code 0f 38 39 */

	asm volatile("vpminsd %xmm1,%xmm2,%xmm3");
	asm volatile("vpminsd %zmm4,%zmm5,%zmm6");
	asm volatile("vpminsq %zmm4,%zmm5,%zmm6");
	asm volatile("vpmovd2m %zmm6,%k5");
	asm volatile("vpmovq2m %zmm6,%k5");

	/* AVX-512: Op code 0f 38 3a */

	asm volatile("vpminuw %ymm4,%ymm6,%ymm2");
	asm volatile("vpbroadcastmw2d %k6,%zmm6");

	/* AVX-512: Op code 0f 38 3b */

	asm volatile("vpminud %ymm4,%ymm6,%ymm2");
	asm volatile("vpminud %zmm4,%zmm5,%zmm6");
	asm volatile("vpminuq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 3d */

	asm volatile("vpmaxsd %ymm4,%ymm6,%ymm2");
	asm volatile("vpmaxsd %zmm4,%zmm5,%zmm6");
	asm volatile("vpmaxsq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 3f */

	asm volatile("vpmaxud %ymm4,%ymm6,%ymm2");
	asm volatile("vpmaxud %zmm4,%zmm5,%zmm6");
	asm volatile("vpmaxuq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 40 */

	asm volatile("vpmulld %ymm4,%ymm6,%ymm2");
	asm volatile("vpmulld %zmm4,%zmm5,%zmm6");
	asm volatile("vpmullq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 42 */

	asm volatile("vgetexpps %zmm5,%zmm6");
	asm volatile("vgetexppd %zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 43 */

	asm volatile("vgetexpss %xmm4,%xmm5,%xmm6{%k7}");
	asm volatile("vgetexpsd %xmm2,%xmm3,%xmm4{%k7}");

	/* AVX-512: Op code 0f 38 44 */

	asm volatile("vplzcntd %zmm5,%zmm6");
	asm volatile("vplzcntq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 46 */

	asm volatile("vpsravd %ymm4,%ymm6,%ymm2");
	asm volatile("vpsravd %zmm4,%zmm5,%zmm6");
	asm volatile("vpsravq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 4c */

	asm volatile("vrcp14ps %zmm5,%zmm6");
	asm volatile("vrcp14pd %zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 4d */

	asm volatile("vrcp14ss %xmm4,%xmm5,%xmm6{%k7}");
	asm volatile("vrcp14sd %xmm4,%xmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 4e */

	asm volatile("vrsqrt14ps %zmm5,%zmm6");
	asm volatile("vrsqrt14pd %zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 4f */

	asm volatile("vrsqrt14ss %xmm4,%xmm5,%xmm6{%k7}");
	asm volatile("vrsqrt14sd %xmm4,%xmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 38 50 */

	asm volatile("vpdpbusd %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpbusd %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpbusd %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpbusd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 51 */

	asm volatile("vpdpbusds %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpbusds %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpbusds %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpbusds 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 52 */

	asm volatile("vdpbf16ps %xmm1, %xmm2, %xmm3");
	asm volatile("vdpbf16ps %ymm1, %ymm2, %ymm3");
	asm volatile("vdpbf16ps %zmm1, %zmm2, %zmm3");
	asm volatile("vdpbf16ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpdpwssd %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpwssd %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpwssd %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpwssd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vp4dpwssd (%eax), %zmm0, %zmm4");
	asm volatile("vp4dpwssd 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 53 */

	asm volatile("vpdpwssds %xmm1, %xmm2, %xmm3");
	asm volatile("vpdpwssds %ymm1, %ymm2, %ymm3");
	asm volatile("vpdpwssds %zmm1, %zmm2, %zmm3");
	asm volatile("vpdpwssds 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vp4dpwssds (%eax), %zmm0, %zmm4");
	asm volatile("vp4dpwssds 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 54 */

	asm volatile("vpopcntb %xmm1, %xmm2");
	asm volatile("vpopcntb %ymm1, %ymm2");
	asm volatile("vpopcntb %zmm1, %zmm2");
	asm volatile("vpopcntb 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpopcntw %xmm1, %xmm2");
	asm volatile("vpopcntw %ymm1, %ymm2");
	asm volatile("vpopcntw %zmm1, %zmm2");
	asm volatile("vpopcntw 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 55 */

	asm volatile("vpopcntd %xmm1, %xmm2");
	asm volatile("vpopcntd %ymm1, %ymm2");
	asm volatile("vpopcntd %zmm1, %zmm2");
	asm volatile("vpopcntd 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpopcntq %xmm1, %xmm2");
	asm volatile("vpopcntq %ymm1, %ymm2");
	asm volatile("vpopcntq %zmm1, %zmm2");
	asm volatile("vpopcntq 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 59 */

	asm volatile("vpbroadcastq %xmm4,%xmm6");
	asm volatile("vbroadcasti32x2 %xmm7,%zmm6");

	/* AVX-512: Op code 0f 38 5a */

	asm volatile("vbroadcasti128 (%ecx),%ymm4");
	asm volatile("vbroadcasti32x4 (%ecx),%zmm6");
	asm volatile("vbroadcasti64x2 (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 5b */

	asm volatile("vbroadcasti32x8 (%ecx),%zmm6");
	asm volatile("vbroadcasti64x4 (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 62 */

	asm volatile("vpexpandb %xmm1, %xmm2");
	asm volatile("vpexpandb %ymm1, %ymm2");
	asm volatile("vpexpandb %zmm1, %zmm2");
	asm volatile("vpexpandb 0x12345678(%eax,%ecx,8),%zmm2");

	asm volatile("vpexpandw %xmm1, %xmm2");
	asm volatile("vpexpandw %ymm1, %ymm2");
	asm volatile("vpexpandw %zmm1, %zmm2");
	asm volatile("vpexpandw 0x12345678(%eax,%ecx,8),%zmm2");

	/* AVX-512: Op code 0f 38 63 */

	asm volatile("vpcompressb %xmm1, %xmm2");
	asm volatile("vpcompressb %ymm1, %ymm2");
	asm volatile("vpcompressb %zmm1, %zmm2");
	asm volatile("vpcompressb %zmm2,0x12345678(%eax,%ecx,8)");

	asm volatile("vpcompressw %xmm1, %xmm2");
	asm volatile("vpcompressw %ymm1, %ymm2");
	asm volatile("vpcompressw %zmm1, %zmm2");
	asm volatile("vpcompressw %zmm2,0x12345678(%eax,%ecx,8)");

	/* AVX-512: Op code 0f 38 64 */

	asm volatile("vpblendmd %zmm4,%zmm5,%zmm6");
	asm volatile("vpblendmq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 65 */

	asm volatile("vblendmps %zmm4,%zmm5,%zmm6");
	asm volatile("vblendmpd %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 66 */

	asm volatile("vpblendmb %zmm4,%zmm5,%zmm6");
	asm volatile("vpblendmw %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 68 */

	asm volatile("vp2intersectd %xmm1, %xmm2, %k3");
	asm volatile("vp2intersectd %ymm1, %ymm2, %k3");
	asm volatile("vp2intersectd %zmm1, %zmm2, %k3");
	asm volatile("vp2intersectd 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	asm volatile("vp2intersectq %xmm1, %xmm2, %k3");
	asm volatile("vp2intersectq %ymm1, %ymm2, %k3");
	asm volatile("vp2intersectq %zmm1, %zmm2, %k3");
	asm volatile("vp2intersectq 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	/* AVX-512: Op code 0f 38 70 */

	asm volatile("vpshldvw %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvw %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvw %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvw 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 71 */

	asm volatile("vpshldvd %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvd %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvd %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpshldvq %xmm1, %xmm2, %xmm3");
	asm volatile("vpshldvq %ymm1, %ymm2, %ymm3");
	asm volatile("vpshldvq %zmm1, %zmm2, %zmm3");
	asm volatile("vpshldvq 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 72 */

	asm volatile("vcvtne2ps2bf16 %xmm1, %xmm2, %xmm3");
	asm volatile("vcvtne2ps2bf16 %ymm1, %ymm2, %ymm3");
	asm volatile("vcvtne2ps2bf16 %zmm1, %zmm2, %zmm3");
	asm volatile("vcvtne2ps2bf16 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vcvtneps2bf16 %xmm1, %xmm2");
	asm volatile("vcvtneps2bf16 %ymm1, %xmm2");
	asm volatile("vcvtneps2bf16 %zmm1, %ymm2");
	asm volatile("vcvtneps2bf16 0x12345678(%eax,%ecx,8),%ymm2");

	asm volatile("vpshrdvw %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvw %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvw %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvw 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 73 */

	asm volatile("vpshrdvd %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvd %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvd %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vpshrdvq %xmm1, %xmm2, %xmm3");
	asm volatile("vpshrdvq %ymm1, %ymm2, %ymm3");
	asm volatile("vpshrdvq %zmm1, %zmm2, %zmm3");
	asm volatile("vpshrdvq 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 75 */

	asm volatile("vpermi2b %zmm4,%zmm5,%zmm6");
	asm volatile("vpermi2w %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 76 */

	asm volatile("vpermi2d %zmm4,%zmm5,%zmm6");
	asm volatile("vpermi2q %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 77 */

	asm volatile("vpermi2ps %zmm4,%zmm5,%zmm6");
	asm volatile("vpermi2pd %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 7a */

	asm volatile("vpbroadcastb %eax,%xmm3");

	/* AVX-512: Op code 0f 38 7b */

	asm volatile("vpbroadcastw %eax,%xmm3");

	/* AVX-512: Op code 0f 38 7c */

	asm volatile("vpbroadcastd %eax,%xmm3");

	/* AVX-512: Op code 0f 38 7d */

	asm volatile("vpermt2b %zmm4,%zmm5,%zmm6");
	asm volatile("vpermt2w %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 7e */

	asm volatile("vpermt2d %zmm4,%zmm5,%zmm6");
	asm volatile("vpermt2q %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 7f */

	asm volatile("vpermt2ps %zmm4,%zmm5,%zmm6");
	asm volatile("vpermt2pd %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 83 */

	asm volatile("vpmultishiftqb %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 88 */

	asm volatile("vexpandps (%ecx),%zmm6");
	asm volatile("vexpandpd (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 89 */

	asm volatile("vpexpandd (%ecx),%zmm6");
	asm volatile("vpexpandq (%ecx),%zmm6");

	/* AVX-512: Op code 0f 38 8a */

	asm volatile("vcompressps %zmm6,(%ecx)");
	asm volatile("vcompresspd %zmm6,(%ecx)");

	/* AVX-512: Op code 0f 38 8b */

	asm volatile("vpcompressd %zmm6,(%ecx)");
	asm volatile("vpcompressq %zmm6,(%ecx)");

	/* AVX-512: Op code 0f 38 8d */

	asm volatile("vpermb %zmm4,%zmm5,%zmm6");
	asm volatile("vpermw %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 8f */

	asm volatile("vpshufbitqmb %xmm1, %xmm2, %k3");
	asm volatile("vpshufbitqmb %ymm1, %ymm2, %k3");
	asm volatile("vpshufbitqmb %zmm1, %zmm2, %k3");
	asm volatile("vpshufbitqmb 0x12345678(%eax,%ecx,8),%zmm2,%k3");

	/* AVX-512: Op code 0f 38 90 */

	asm volatile("vpgatherdd %xmm2,0x02(%ebp,%xmm7,2),%xmm1");
	asm volatile("vpgatherdq %xmm2,0x04(%ebp,%xmm7,2),%xmm1");
	asm volatile("vpgatherdd 0x7b(%ebp,%zmm7,8),%zmm6{%k1}");
	asm volatile("vpgatherdq 0x7b(%ebp,%ymm7,8),%zmm6{%k1}");

	/* AVX-512: Op code 0f 38 91 */

	asm volatile("vpgatherqd %xmm2,0x02(%ebp,%xmm7,2),%xmm1");
	asm volatile("vpgatherqq %xmm2,0x02(%ebp,%xmm7,2),%xmm1");
	asm volatile("vpgatherqd 0x7b(%ebp,%zmm7,8),%ymm6{%k1}");
	asm volatile("vpgatherqq 0x7b(%ebp,%zmm7,8),%zmm6{%k1}");

	/* AVX-512: Op code 0f 38 9a */

	asm volatile("vfmsub132ps %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132ps %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub132ps %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub132ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vfmsub132pd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132pd %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub132pd %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub132pd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("v4fmaddps (%eax), %zmm0, %zmm4");
	asm volatile("v4fmaddps 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 9b */

	asm volatile("vfmsub132ss %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132ss 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("vfmsub132sd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub132sd 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("v4fmaddss (%eax), %xmm0, %xmm4");
	asm volatile("v4fmaddss 0x12345678(%eax,%ecx,8),%xmm0,%xmm4");

	/* AVX-512: Op code 0f 38 a0 */

	asm volatile("vpscatterdd %zmm6,0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vpscatterdq %zmm6,0x7b(%ebp,%ymm7,8){%k1}");

	/* AVX-512: Op code 0f 38 a1 */

	asm volatile("vpscatterqd %ymm6,0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vpscatterqq %ymm6,0x7b(%ebp,%ymm7,8){%k1}");

	/* AVX-512: Op code 0f 38 a2 */

	asm volatile("vscatterdps %zmm6,0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterdpd %zmm6,0x7b(%ebp,%ymm7,8){%k1}");

	/* AVX-512: Op code 0f 38 a3 */

	asm volatile("vscatterqps %ymm6,0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterqpd %zmm6,0x7b(%ebp,%zmm7,8){%k1}");

	/* AVX-512: Op code 0f 38 aa */

	asm volatile("vfmsub213ps %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213ps %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub213ps %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub213ps 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("vfmsub213pd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213pd %ymm1, %ymm2, %ymm3");
	asm volatile("vfmsub213pd %zmm1, %zmm2, %zmm3");
	asm volatile("vfmsub213pd 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	asm volatile("v4fnmaddps (%eax), %zmm0, %zmm4");
	asm volatile("v4fnmaddps 0x12345678(%eax,%ecx,8),%zmm0,%zmm4");

	/* AVX-512: Op code 0f 38 ab */

	asm volatile("vfmsub213ss %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213ss 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("vfmsub213sd %xmm1, %xmm2, %xmm3");
	asm volatile("vfmsub213sd 0x12345678(%eax,%ecx,8),%xmm2,%xmm3");

	asm volatile("v4fnmaddss (%eax), %xmm0, %xmm4");
	asm volatile("v4fnmaddss 0x12345678(%eax,%ecx,8),%xmm0,%xmm4");

	/* AVX-512: Op code 0f 38 b4 */

	asm volatile("vpmadd52luq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 b5 */

	asm volatile("vpmadd52huq %zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 c4 */

	asm volatile("vpconflictd %zmm5,%zmm6");
	asm volatile("vpconflictq %zmm5,%zmm6");

	/* AVX-512: Op code 0f 38 c8 */

	asm volatile("vexp2ps %zmm6,%zmm7");
	asm volatile("vexp2pd %zmm6,%zmm7");

	/* AVX-512: Op code 0f 38 ca */

	asm volatile("vrcp28ps %zmm6,%zmm7");
	asm volatile("vrcp28pd %zmm6,%zmm7");

	/* AVX-512: Op code 0f 38 cb */

	asm volatile("vrcp28ss %xmm5,%xmm6,%xmm7{%k7}");
	asm volatile("vrcp28sd %xmm5,%xmm6,%xmm7{%k7}");

	/* AVX-512: Op code 0f 38 cc */

	asm volatile("vrsqrt28ps %zmm6,%zmm7");
	asm volatile("vrsqrt28pd %zmm6,%zmm7");

	/* AVX-512: Op code 0f 38 cd */

	asm volatile("vrsqrt28ss %xmm5,%xmm6,%xmm7{%k7}");
	asm volatile("vrsqrt28sd %xmm5,%xmm6,%xmm7{%k7}");

	/* AVX-512: Op code 0f 38 cf */

	asm volatile("gf2p8mulb %xmm1, %xmm3");
	asm volatile("gf2p8mulb 0x12345678(%eax,%ecx,8),%xmm3");

	asm volatile("vgf2p8mulb %xmm1, %xmm2, %xmm3");
	asm volatile("vgf2p8mulb %ymm1, %ymm2, %ymm3");
	asm volatile("vgf2p8mulb %zmm1, %zmm2, %zmm3");
	asm volatile("vgf2p8mulb 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 dc */

	asm volatile("vaesenc %xmm1, %xmm2, %xmm3");
	asm volatile("vaesenc %ymm1, %ymm2, %ymm3");
	asm volatile("vaesenc %zmm1, %zmm2, %zmm3");
	asm volatile("vaesenc 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 dd */

	asm volatile("vaesenclast %xmm1, %xmm2, %xmm3");
	asm volatile("vaesenclast %ymm1, %ymm2, %ymm3");
	asm volatile("vaesenclast %zmm1, %zmm2, %zmm3");
	asm volatile("vaesenclast 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 de */

	asm volatile("vaesdec %xmm1, %xmm2, %xmm3");
	asm volatile("vaesdec %ymm1, %ymm2, %ymm3");
	asm volatile("vaesdec %zmm1, %zmm2, %zmm3");
	asm volatile("vaesdec 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 38 df */

	asm volatile("vaesdeclast %xmm1, %xmm2, %xmm3");
	asm volatile("vaesdeclast %ymm1, %ymm2, %ymm3");
	asm volatile("vaesdeclast %zmm1, %zmm2, %zmm3");
	asm volatile("vaesdeclast 0x12345678(%eax,%ecx,8),%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 03 */

	asm volatile("valignd $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("valignq $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 08 */

	asm volatile("vroundps $0x5,%ymm6,%ymm2");
	asm volatile("vrndscaleps $0x12,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 3a 09 */

	asm volatile("vroundpd $0x5,%ymm6,%ymm2");
	asm volatile("vrndscalepd $0x12,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 3a 0a */

	asm volatile("vroundss $0x5,%xmm4,%xmm6,%xmm2");
	asm volatile("vrndscaless $0x12,%xmm4,%xmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 3a 0b */

	asm volatile("vroundsd $0x5,%xmm4,%xmm6,%xmm2");
	asm volatile("vrndscalesd $0x12,%xmm4,%xmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 3a 18 */

	asm volatile("vinsertf128 $0x5,%xmm4,%ymm4,%ymm6");
	asm volatile("vinsertf32x4 $0x12,%xmm4,%zmm5,%zmm6{%k7}");
	asm volatile("vinsertf64x2 $0x12,%xmm4,%zmm5,%zmm6{%k7}");

	/* AVX-512: Op code 0f 3a 19 */

	asm volatile("vextractf128 $0x5,%ymm4,%xmm4");
	asm volatile("vextractf32x4 $0x12,%zmm5,%xmm6{%k7}");
	asm volatile("vextractf64x2 $0x12,%zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 3a 1a */

	asm volatile("vinsertf32x8 $0x12,%ymm5,%zmm6,%zmm7{%k7}");
	asm volatile("vinsertf64x4 $0x12,%ymm5,%zmm6,%zmm7{%k7}");

	/* AVX-512: Op code 0f 3a 1b */

	asm volatile("vextractf32x8 $0x12,%zmm6,%ymm7{%k7}");
	asm volatile("vextractf64x4 $0x12,%zmm6,%ymm7{%k7}");

	/* AVX-512: Op code 0f 3a 1e */

	asm volatile("vpcmpud $0x12,%zmm6,%zmm7,%k5");
	asm volatile("vpcmpuq $0x12,%zmm6,%zmm7,%k5");

	/* AVX-512: Op code 0f 3a 1f */

	asm volatile("vpcmpd $0x12,%zmm6,%zmm7,%k5");
	asm volatile("vpcmpq $0x12,%zmm6,%zmm7,%k5");

	/* AVX-512: Op code 0f 3a 23 */

	asm volatile("vshuff32x4 $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("vshuff64x2 $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 25 */

	asm volatile("vpternlogd $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("vpternlogq $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 26 */

	asm volatile("vgetmantps $0x12,%zmm6,%zmm7");
	asm volatile("vgetmantpd $0x12,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 27 */

	asm volatile("vgetmantss $0x12,%xmm5,%xmm6,%xmm7{%k7}");
	asm volatile("vgetmantsd $0x12,%xmm5,%xmm6,%xmm7{%k7}");

	/* AVX-512: Op code 0f 3a 38 */

	asm volatile("vinserti128 $0x5,%xmm4,%ymm4,%ymm6");
	asm volatile("vinserti32x4 $0x12,%xmm4,%zmm5,%zmm6{%k7}");
	asm volatile("vinserti64x2 $0x12,%xmm4,%zmm5,%zmm6{%k7}");

	/* AVX-512: Op code 0f 3a 39 */

	asm volatile("vextracti128 $0x5,%ymm4,%xmm6");
	asm volatile("vextracti32x4 $0x12,%zmm5,%xmm6{%k7}");
	asm volatile("vextracti64x2 $0x12,%zmm5,%xmm6{%k7}");

	/* AVX-512: Op code 0f 3a 3a */

	asm volatile("vinserti32x8 $0x12,%ymm5,%zmm6,%zmm7{%k7}");
	asm volatile("vinserti64x4 $0x12,%ymm5,%zmm6,%zmm7{%k7}");

	/* AVX-512: Op code 0f 3a 3b */

	asm volatile("vextracti32x8 $0x12,%zmm6,%ymm7{%k7}");
	asm volatile("vextracti64x4 $0x12,%zmm6,%ymm7{%k7}");

	/* AVX-512: Op code 0f 3a 3e */

	asm volatile("vpcmpub $0x12,%zmm6,%zmm7,%k5");
	asm volatile("vpcmpuw $0x12,%zmm6,%zmm7,%k5");

	/* AVX-512: Op code 0f 3a 3f */

	asm volatile("vpcmpb $0x12,%zmm6,%zmm7,%k5");
	asm volatile("vpcmpw $0x12,%zmm6,%zmm7,%k5");

	/* AVX-512: Op code 0f 3a 42 */

	asm volatile("vmpsadbw $0x5,%ymm4,%ymm6,%ymm2");
	asm volatile("vdbpsadbw $0x12,%zmm4,%zmm5,%zmm6");

	/* AVX-512: Op code 0f 3a 43 */

	asm volatile("vshufi32x4 $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("vshufi64x2 $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 44 */

	asm volatile("vpclmulqdq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpclmulqdq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpclmulqdq $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 50 */

	asm volatile("vrangeps $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("vrangepd $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 51 */

	asm volatile("vrangess $0x12,%xmm5,%xmm6,%xmm7");
	asm volatile("vrangesd $0x12,%xmm5,%xmm6,%xmm7");

	/* AVX-512: Op code 0f 3a 54 */

	asm volatile("vfixupimmps $0x12,%zmm5,%zmm6,%zmm7");
	asm volatile("vfixupimmpd $0x12,%zmm5,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 55 */

	asm volatile("vfixupimmss $0x12,%xmm5,%xmm6,%xmm7{%k7}");
	asm volatile("vfixupimmsd $0x12,%xmm5,%xmm6,%xmm7{%k7}");

	/* AVX-512: Op code 0f 3a 56 */

	asm volatile("vreduceps $0x12,%zmm6,%zmm7");
	asm volatile("vreducepd $0x12,%zmm6,%zmm7");

	/* AVX-512: Op code 0f 3a 57 */

	asm volatile("vreducess $0x12,%xmm5,%xmm6,%xmm7");
	asm volatile("vreducesd $0x12,%xmm5,%xmm6,%xmm7");

	/* AVX-512: Op code 0f 3a 66 */

	asm volatile("vfpclassps $0x12,%zmm7,%k5");
	asm volatile("vfpclasspd $0x12,%zmm7,%k5");

	/* AVX-512: Op code 0f 3a 67 */

	asm volatile("vfpclassss $0x12,%xmm7,%k5");
	asm volatile("vfpclasssd $0x12,%xmm7,%k5");

	/* AVX-512: Op code 0f 3a 70 */

	asm volatile("vpshldw $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldw $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldw $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 71 */

	asm volatile("vpshldd $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldd $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldd $0x12,%zmm1,%zmm2,%zmm3");

	asm volatile("vpshldq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshldq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshldq $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 72 */

	asm volatile("vpshrdw $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdw $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdw $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a 73 */

	asm volatile("vpshrdd $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdd $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdd $0x12,%zmm1,%zmm2,%zmm3");

	asm volatile("vpshrdq $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vpshrdq $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vpshrdq $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a ce */

	asm volatile("gf2p8affineqb $0x12,%xmm1,%xmm3");

	asm volatile("vgf2p8affineqb $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vgf2p8affineqb $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vgf2p8affineqb $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 3a cf */

	asm volatile("gf2p8affineinvqb $0x12,%xmm1,%xmm3");

	asm volatile("vgf2p8affineinvqb $0x12,%xmm1,%xmm2,%xmm3");
	asm volatile("vgf2p8affineinvqb $0x12,%ymm1,%ymm2,%ymm3");
	asm volatile("vgf2p8affineinvqb $0x12,%zmm1,%zmm2,%zmm3");

	/* AVX-512: Op code 0f 72 (Grp13) */

	asm volatile("vprord $0x12,%zmm5,%zmm6");
	asm volatile("vprorq $0x12,%zmm5,%zmm6");
	asm volatile("vprold $0x12,%zmm5,%zmm6");
	asm volatile("vprolq $0x12,%zmm5,%zmm6");
	asm volatile("psrad  $0x2,%mm6");
	asm volatile("vpsrad $0x5,%ymm6,%ymm2");
	asm volatile("vpsrad $0x5,%zmm6,%zmm2");
	asm volatile("vpsraq $0x5,%zmm6,%zmm2");

	/* AVX-512: Op code 0f 38 c6 (Grp18) */

	asm volatile("vgatherpf0dps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vgatherpf0dpd 0x7b(%ebp,%ymm7,8){%k1}");
	asm volatile("vgatherpf1dps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vgatherpf1dpd 0x7b(%ebp,%ymm7,8){%k1}");
	asm volatile("vscatterpf0dps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf0dpd 0x7b(%ebp,%ymm7,8){%k1}");
	asm volatile("vscatterpf1dps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf1dpd 0x7b(%ebp,%ymm7,8){%k1}");

	/* AVX-512: Op code 0f 38 c7 (Grp19) */

	asm volatile("vgatherpf0qps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vgatherpf0qpd 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vgatherpf1qps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vgatherpf1qpd 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf0qps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf0qpd 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf1qps 0x7b(%ebp,%zmm7,8){%k1}");
	asm volatile("vscatterpf1qpd 0x7b(%ebp,%zmm7,8){%k1}");

	/* AVX-512: Examples */

	asm volatile("vaddpd %zmm4,%zmm5,%zmm6");
	asm volatile("vaddpd %zmm4,%zmm5,%zmm6{%k7}");
	asm volatile("vaddpd %zmm4,%zmm5,%zmm6{%k7}{z}");
	asm volatile("vaddpd {rn-sae},%zmm4,%zmm5,%zmm6");
	asm volatile("vaddpd {ru-sae},%zmm4,%zmm5,%zmm6");
	asm volatile("vaddpd {rd-sae},%zmm4,%zmm5,%zmm6");
	asm volatile("vaddpd {rz-sae},%zmm4,%zmm5,%zmm6");
	asm volatile("vaddpd (%ecx),%zmm5,%zmm6");
	asm volatile("vaddpd 0x123(%eax,%ecx,8),%zmm5,%zmm6");
	asm volatile("vaddpd (%ecx){1to8},%zmm5,%zmm6");
	asm volatile("vaddpd 0x1fc0(%edx),%zmm5,%zmm6");
	asm volatile("vaddpd 0x3f8(%edx){1to8},%zmm5,%zmm6");
	asm volatile("vcmpeq_uqps 0x1fc(%edx){1to16},%zmm6,%k5");
	asm volatile("vcmpltsd 0x123(%eax,%ecx,8),%xmm3,%k5{%k7}");
	asm volatile("vcmplesd {sae},%xmm4,%xmm5,%k5{%k7}");
	asm volatile("vgetmantss $0x5b,0x123(%eax,%ecx,8),%xmm4,%xmm5{%k7}");

	/* bndmk m32, bnd */

	asm volatile("bndmk (%eax), %bnd0");
	asm volatile("bndmk (0x12345678), %bnd0");
	asm volatile("bndmk (%eax), %bnd3");
	asm volatile("bndmk (%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndmk (%eax,%ecx,1), %bnd0");
	asm volatile("bndmk (%eax,%ecx,8), %bnd0");
	asm volatile("bndmk 0x12(%eax), %bnd0");
	asm volatile("bndmk 0x12(%ebp), %bnd0");
	asm volatile("bndmk 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndmk 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndmk 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndmk 0x12345678(%eax), %bnd0");
	asm volatile("bndmk 0x12345678(%ebp), %bnd0");
	asm volatile("bndmk 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndmk 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndmk 0x12345678(%eax,%ecx,8), %bnd0");

	/* bndcl r/m32, bnd */

	asm volatile("bndcl (%eax), %bnd0");
	asm volatile("bndcl (0x12345678), %bnd0");
	asm volatile("bndcl (%eax), %bnd3");
	asm volatile("bndcl (%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcl (%eax,%ecx,1), %bnd0");
	asm volatile("bndcl (%eax,%ecx,8), %bnd0");
	asm volatile("bndcl 0x12(%eax), %bnd0");
	asm volatile("bndcl 0x12(%ebp), %bnd0");
	asm volatile("bndcl 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcl 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcl 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcl 0x12345678(%eax), %bnd0");
	asm volatile("bndcl 0x12345678(%ebp), %bnd0");
	asm volatile("bndcl 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcl 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcl 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcl %eax, %bnd0");

	/* bndcu r/m32, bnd */

	asm volatile("bndcu (%eax), %bnd0");
	asm volatile("bndcu (0x12345678), %bnd0");
	asm volatile("bndcu (%eax), %bnd3");
	asm volatile("bndcu (%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcu (%eax,%ecx,1), %bnd0");
	asm volatile("bndcu (%eax,%ecx,8), %bnd0");
	asm volatile("bndcu 0x12(%eax), %bnd0");
	asm volatile("bndcu 0x12(%ebp), %bnd0");
	asm volatile("bndcu 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcu 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcu 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcu 0x12345678(%eax), %bnd0");
	asm volatile("bndcu 0x12345678(%ebp), %bnd0");
	asm volatile("bndcu 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcu 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcu 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcu %eax, %bnd0");

	/* bndcn r/m32, bnd */

	asm volatile("bndcn (%eax), %bnd0");
	asm volatile("bndcn (0x12345678), %bnd0");
	asm volatile("bndcn (%eax), %bnd3");
	asm volatile("bndcn (%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndcn (%eax,%ecx,1), %bnd0");
	asm volatile("bndcn (%eax,%ecx,8), %bnd0");
	asm volatile("bndcn 0x12(%eax), %bnd0");
	asm volatile("bndcn 0x12(%ebp), %bnd0");
	asm volatile("bndcn 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndcn 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndcn 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndcn 0x12345678(%eax), %bnd0");
	asm volatile("bndcn 0x12345678(%ebp), %bnd0");
	asm volatile("bndcn 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndcn 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndcn 0x12345678(%eax,%ecx,8), %bnd0");
	asm volatile("bndcn %eax, %bnd0");

	/* bndmov m64, bnd */

	asm volatile("bndmov (%eax), %bnd0");
	asm volatile("bndmov (0x12345678), %bnd0");
	asm volatile("bndmov (%eax), %bnd3");
	asm volatile("bndmov (%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndmov (%eax,%ecx,1), %bnd0");
	asm volatile("bndmov (%eax,%ecx,8), %bnd0");
	asm volatile("bndmov 0x12(%eax), %bnd0");
	asm volatile("bndmov 0x12(%ebp), %bnd0");
	asm volatile("bndmov 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndmov 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndmov 0x12(%eax,%ecx,8), %bnd0");
	asm volatile("bndmov 0x12345678(%eax), %bnd0");
	asm volatile("bndmov 0x12345678(%ebp), %bnd0");
	asm volatile("bndmov 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndmov 0x12345678(%eax,%ecx,1), %bnd0");
	asm volatile("bndmov 0x12345678(%eax,%ecx,8), %bnd0");

	/* bndmov bnd, m64 */

	asm volatile("bndmov %bnd0, (%eax)");
	asm volatile("bndmov %bnd0, (0x12345678)");
	asm volatile("bndmov %bnd3, (%eax)");
	asm volatile("bndmov %bnd0, (%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(,%eax,1)");
	asm volatile("bndmov %bnd0, (%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, (%eax,%ecx,8)");
	asm volatile("bndmov %bnd0, 0x12(%eax)");
	asm volatile("bndmov %bnd0, 0x12(%ebp)");
	asm volatile("bndmov %bnd0, 0x12(%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12(%ebp,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12(%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, 0x12(%eax,%ecx,8)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax)");
	asm volatile("bndmov %bnd0, 0x12345678(%ebp)");
	asm volatile("bndmov %bnd0, 0x12345678(%ecx,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%ebp,%eax,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax,%ecx,1)");
	asm volatile("bndmov %bnd0, 0x12345678(%eax,%ecx,8)");

	/* bndmov bnd2, bnd1 */

	asm volatile("bndmov %bnd0, %bnd1");
	asm volatile("bndmov %bnd1, %bnd0");

	/* bndldx mib, bnd */

	asm volatile("bndldx (%eax), %bnd0");
	asm volatile("bndldx (0x12345678), %bnd0");
	asm volatile("bndldx (%eax), %bnd3");
	asm volatile("bndldx (%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(,%eax,1), %bnd0");
	asm volatile("bndldx (%eax,%ecx,1), %bnd0");
	asm volatile("bndldx 0x12(%eax), %bnd0");
	asm volatile("bndldx 0x12(%ebp), %bnd0");
	asm volatile("bndldx 0x12(%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12(%ebp,%eax,1), %bnd0");
	asm volatile("bndldx 0x12(%eax,%ecx,1), %bnd0");
	asm volatile("bndldx 0x12345678(%eax), %bnd0");
	asm volatile("bndldx 0x12345678(%ebp), %bnd0");
	asm volatile("bndldx 0x12345678(%ecx,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%ebp,%eax,1), %bnd0");
	asm volatile("bndldx 0x12345678(%eax,%ecx,1), %bnd0");

	/* bndstx bnd, mib */

	asm volatile("bndstx %bnd0, (%eax)");
	asm volatile("bndstx %bnd0, (0x12345678)");
	asm volatile("bndstx %bnd3, (%eax)");
	asm volatile("bndstx %bnd0, (%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(,%eax,1)");
	asm volatile("bndstx %bnd0, (%eax,%ecx,1)");
	asm volatile("bndstx %bnd0, 0x12(%eax)");
	asm volatile("bndstx %bnd0, 0x12(%ebp)");
	asm volatile("bndstx %bnd0, 0x12(%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12(%ebp,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12(%eax,%ecx,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%eax)");
	asm volatile("bndstx %bnd0, 0x12345678(%ebp)");
	asm volatile("bndstx %bnd0, 0x12345678(%ecx,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%ebp,%eax,1)");
	asm volatile("bndstx %bnd0, 0x12345678(%eax,%ecx,1)");

	/* bnd prefix on call, ret, jmp and all jcc */

	asm volatile("bnd call label1");  /* Expecting: call unconditional 0xfffffffc */
	asm volatile("bnd call *(%eax)"); /* Expecting: call indirect      0 */
	asm volatile("bnd ret");          /* Expecting: ret  indirect      0 */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0xfffffffc */
	asm volatile("bnd jmp label1");   /* Expecting: jmp  unconditional 0xfffffffc */
	asm volatile("bnd jmp *(%ecx)");  /* Expecting: jmp  indirect      0 */
	asm volatile("bnd jne label1");   /* Expecting: jcc  conditional   0xfffffffc */

	/* sha1rnds4 imm8, xmm2/m128, xmm1 */

	asm volatile("sha1rnds4 $0x0, %xmm1, %xmm0");
	asm volatile("sha1rnds4 $0x91, %xmm7, %xmm2");
	asm volatile("sha1rnds4 $0x91, (%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, (0x12345678), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax), %xmm3");
	asm volatile("sha1rnds4 $0x91, (%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, (%eax,%ecx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ebp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ebp), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1rnds4 $0x91, 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1nexte xmm2/m128, xmm1 */

	asm volatile("sha1nexte %xmm1, %xmm0");
	asm volatile("sha1nexte %xmm7, %xmm2");
	asm volatile("sha1nexte (%eax), %xmm0");
	asm volatile("sha1nexte (0x12345678), %xmm0");
	asm volatile("sha1nexte (%eax), %xmm3");
	asm volatile("sha1nexte (%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1nexte (%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte (%eax,%ecx,8), %xmm0");
	asm volatile("sha1nexte 0x12(%eax), %xmm0");
	asm volatile("sha1nexte 0x12(%ebp), %xmm0");
	asm volatile("sha1nexte 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ebp), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1nexte 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1msg1 xmm2/m128, xmm1 */

	asm volatile("sha1msg1 %xmm1, %xmm0");
	asm volatile("sha1msg1 %xmm7, %xmm2");
	asm volatile("sha1msg1 (%eax), %xmm0");
	asm volatile("sha1msg1 (0x12345678), %xmm0");
	asm volatile("sha1msg1 (%eax), %xmm3");
	asm volatile("sha1msg1 (%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1msg1 (%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 (%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg1 0x12(%eax), %xmm0");
	asm volatile("sha1msg1 0x12(%ebp), %xmm0");
	asm volatile("sha1msg1 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ebp), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg1 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha1msg2 xmm2/m128, xmm1 */

	asm volatile("sha1msg2 %xmm1, %xmm0");
	asm volatile("sha1msg2 %xmm7, %xmm2");
	asm volatile("sha1msg2 (%eax), %xmm0");
	asm volatile("sha1msg2 (0x12345678), %xmm0");
	asm volatile("sha1msg2 (%eax), %xmm3");
	asm volatile("sha1msg2 (%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha1msg2 (%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 (%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg2 0x12(%eax), %xmm0");
	asm volatile("sha1msg2 0x12(%ebp), %xmm0");
	asm volatile("sha1msg2 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ebp), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha1msg2 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha256rnds2 <XMM0>, xmm2/m128, xmm1 */
	/* Note sha256rnds2 has an implicit operand 'xmm0' */

	asm volatile("sha256rnds2 %xmm4, %xmm1");
	asm volatile("sha256rnds2 %xmm7, %xmm2");
	asm volatile("sha256rnds2 (%eax), %xmm1");
	asm volatile("sha256rnds2 (0x12345678), %xmm1");
	asm volatile("sha256rnds2 (%eax), %xmm3");
	asm volatile("sha256rnds2 (%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(,%eax,1), %xmm1");
	asm volatile("sha256rnds2 (%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 (%eax,%ecx,8), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax), %xmm1");
	asm volatile("sha256rnds2 0x12(%ebp), %xmm1");
	asm volatile("sha256rnds2 0x12(%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%ebp,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 0x12(%eax,%ecx,8), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ebp), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ecx,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%ebp,%eax,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax,%ecx,1), %xmm1");
	asm volatile("sha256rnds2 0x12345678(%eax,%ecx,8), %xmm1");

	/* sha256msg1 xmm2/m128, xmm1 */

	asm volatile("sha256msg1 %xmm1, %xmm0");
	asm volatile("sha256msg1 %xmm7, %xmm2");
	asm volatile("sha256msg1 (%eax), %xmm0");
	asm volatile("sha256msg1 (0x12345678), %xmm0");
	asm volatile("sha256msg1 (%eax), %xmm3");
	asm volatile("sha256msg1 (%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha256msg1 (%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 (%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg1 0x12(%eax), %xmm0");
	asm volatile("sha256msg1 0x12(%ebp), %xmm0");
	asm volatile("sha256msg1 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ebp), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg1 0x12345678(%eax,%ecx,8), %xmm0");

	/* sha256msg2 xmm2/m128, xmm1 */

	asm volatile("sha256msg2 %xmm1, %xmm0");
	asm volatile("sha256msg2 %xmm7, %xmm2");
	asm volatile("sha256msg2 (%eax), %xmm0");
	asm volatile("sha256msg2 (0x12345678), %xmm0");
	asm volatile("sha256msg2 (%eax), %xmm3");
	asm volatile("sha256msg2 (%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(,%eax,1), %xmm0");
	asm volatile("sha256msg2 (%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 (%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg2 0x12(%eax), %xmm0");
	asm volatile("sha256msg2 0x12(%ebp), %xmm0");
	asm volatile("sha256msg2 0x12(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 0x12(%eax,%ecx,8), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ebp), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ecx,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%ebp,%eax,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax,%ecx,1), %xmm0");
	asm volatile("sha256msg2 0x12345678(%eax,%ecx,8), %xmm0");

	/* clflushopt m8 */

	asm volatile("clflushopt (%eax)");
	asm volatile("clflushopt (0x12345678)");
	asm volatile("clflushopt 0x12345678(%eax,%ecx,8)");
	/* Also check instructions in the same group encoding as clflushopt */
	asm volatile("clflush (%eax)");
	asm volatile("sfence");

	/* clwb m8 */

	asm volatile("clwb (%eax)");
	asm volatile("clwb (0x12345678)");
	asm volatile("clwb 0x12345678(%eax,%ecx,8)");
	/* Also check instructions in the same group encoding as clwb */
	asm volatile("xsaveopt (%eax)");
	asm volatile("mfence");

	/* cldemote m8 */

	asm volatile("cldemote (%eax)");
	asm volatile("cldemote (0x12345678)");
	asm volatile("cldemote 0x12345678(%eax,%ecx,8)");

	/* xsavec mem */

	asm volatile("xsavec (%eax)");
	asm volatile("xsavec (0x12345678)");
	asm volatile("xsavec 0x12345678(%eax,%ecx,8)");

	/* xsaves mem */

	asm volatile("xsaves (%eax)");
	asm volatile("xsaves (0x12345678)");
	asm volatile("xsaves 0x12345678(%eax,%ecx,8)");

	/* xrstors mem */

	asm volatile("xrstors (%eax)");
	asm volatile("xrstors (0x12345678)");
	asm volatile("xrstors 0x12345678(%eax,%ecx,8)");

	/* ptwrite */

	asm volatile("ptwrite (%eax)");
	asm volatile("ptwrite (0x12345678)");
	asm volatile("ptwrite 0x12345678(%eax,%ecx,8)");

	asm volatile("ptwritel (%eax)");
	asm volatile("ptwritel (0x12345678)");
	asm volatile("ptwritel 0x12345678(%eax,%ecx,8)");

	/* tpause */

	asm volatile("tpause %ebx");

	/* umonitor */

	asm volatile("umonitor %ax");
	asm volatile("umonitor %eax");

	/* umwait */

	asm volatile("umwait %eax");

	/* movdiri */

	asm volatile("movdiri %eax,(%ebx)");
	asm volatile("movdiri %ecx,0x12345678(%eax)");

	/* movdir64b */

	asm volatile("movdir64b (%eax),%ebx");
	asm volatile("movdir64b 0x12345678(%eax),%ecx");
	asm volatile("movdir64b (%si),%bx");
	asm volatile("movdir64b 0x1234(%si),%cx");

	/* enqcmd */

	asm volatile("enqcmd (%eax),%ebx");
	asm volatile("enqcmd 0x12345678(%eax),%ecx");
	asm volatile("enqcmd (%si),%bx");
	asm volatile("enqcmd 0x1234(%si),%cx");

	/* enqcmds */

	asm volatile("enqcmds (%eax),%ebx");
	asm volatile("enqcmds 0x12345678(%eax),%ecx");
	asm volatile("enqcmds (%si),%bx");
	asm volatile("enqcmds 0x1234(%si),%cx");

	/* incsspd */

	asm volatile("incsspd %eax");
	/* Also check instructions in the same group encoding as incsspd */
	asm volatile("xrstor (%eax)");
	asm volatile("xrstor (0x12345678)");
	asm volatile("xrstor 0x12345678(%eax,%ecx,8)");
	asm volatile("lfence");

	/* rdsspd */

	asm volatile("rdsspd %eax");

	/* saveprevssp */

	asm volatile("saveprevssp");

	/* rstorssp */

	asm volatile("rstorssp (%eax)");
	asm volatile("rstorssp (0x12345678)");
	asm volatile("rstorssp 0x12345678(%eax,%ecx,8)");

	/* wrssd */

	asm volatile("wrssd %ecx,(%eax)");
	asm volatile("wrssd %edx,(0x12345678)");
	asm volatile("wrssd %edx,0x12345678(%eax,%ecx,8)");

	/* wrussd */

	asm volatile("wrussd %ecx,(%eax)");
	asm volatile("wrussd %edx,(0x12345678)");
	asm volatile("wrussd %edx,0x12345678(%eax,%ecx,8)");

	/* setssbsy */

	asm volatile("setssbsy");
	/* Also check instructions in the same group encoding as setssbsy */
	asm volatile("rdpkru");
	asm volatile("wrpkru");

	/* clrssbsy */

	asm volatile("clrssbsy (%eax)");
	asm volatile("clrssbsy (0x12345678)");
	asm volatile("clrssbsy 0x12345678(%eax,%ecx,8)");

	/* endbr32/64 */

	asm volatile("endbr32");
	asm volatile("endbr64");

	/* call with/without notrack prefix */

	asm volatile("call *%eax");				/* Expecting: call indirect 0 */
	asm volatile("call *(%eax)");				/* Expecting: call indirect 0 */
	asm volatile("call *(0x12345678)");			/* Expecting: call indirect 0 */
	asm volatile("call *0x12345678(%eax,%ecx,8)");		/* Expecting: call indirect 0 */

	asm volatile("bnd call *%eax");				/* Expecting: call indirect 0 */
	asm volatile("bnd call *(%eax)");			/* Expecting: call indirect 0 */
	asm volatile("bnd call *(0x12345678)");			/* Expecting: call indirect 0 */
	asm volatile("bnd call *0x12345678(%eax,%ecx,8)");	/* Expecting: call indirect 0 */

	asm volatile("notrack call *%eax");			/* Expecting: call indirect 0 */
	asm volatile("notrack call *(%eax)");			/* Expecting: call indirect 0 */
	asm volatile("notrack call *(0x12345678)");		/* Expecting: call indirect 0 */
	asm volatile("notrack call *0x12345678(%eax,%ecx,8)");	/* Expecting: call indirect 0 */

	asm volatile("notrack bnd call *%eax");			/* Expecting: call indirect 0 */
	asm volatile("notrack bnd call *(%eax)");		/* Expecting: call indirect 0 */
	asm volatile("notrack bnd call *(0x12345678)");		/* Expecting: call indirect 0 */
	asm volatile("notrack bnd call *0x12345678(%eax,%ecx,8)"); /* Expecting: call indirect 0 */

	/* jmp with/without notrack prefix */

	asm volatile("jmp *%eax");				/* Expecting: jmp indirect 0 */
	asm volatile("jmp *(%eax)");				/* Expecting: jmp indirect 0 */
	asm volatile("jmp *(0x12345678)");			/* Expecting: jmp indirect 0 */
	asm volatile("jmp *0x12345678(%eax,%ecx,8)");		/* Expecting: jmp indirect 0 */

	asm volatile("bnd jmp *%eax");				/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmp *(%eax)");			/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmp *(0x12345678)");			/* Expecting: jmp indirect 0 */
	asm volatile("bnd jmp *0x12345678(%eax,%ecx,8)");	/* Expecting: jmp indirect 0 */

	asm volatile("notrack jmp *%eax");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmp *(%eax)");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmp *(0x12345678)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack jmp *0x12345678(%eax,%ecx,8)");	/* Expecting: jmp indirect 0 */

	asm volatile("notrack bnd jmp *%eax");			/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmp *(%eax)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmp *(0x12345678)");		/* Expecting: jmp indirect 0 */
	asm volatile("notrack bnd jmp *0x12345678(%eax,%ecx,8)"); /* Expecting: jmp indirect 0 */

	/* AVX512-FP16 */

	asm volatile("vaddph %zmm3, %zmm2, %zmm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vaddph %xmm3, %xmm2, %xmm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vaddph %ymm3, %ymm2, %ymm1");
	asm volatile("vaddph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vaddsh %xmm3, %xmm2, %xmm1");
	asm volatile("vaddsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcmpph $0x12, %zmm3, %zmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %zmm2, %k5");
	asm volatile("vcmpph $0x12, %xmm3, %xmm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %k5");
	asm volatile("vcmpph $0x12, %ymm3, %ymm2, %k5");
	asm volatile("vcmpph $0x12, 0x12345678(%eax,%ecx,8), %ymm2, %k5");
	asm volatile("vcmpsh $0x12, %xmm3, %xmm2, %k5");
	asm volatile("vcmpsh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %k5");
	asm volatile("vcomish %xmm2, %xmm1");
	asm volatile("vcomish 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtdq2ph %zmm2, %ymm1");
	asm volatile("vcvtdq2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtdq2ph %xmm2, %xmm1");
	asm volatile("vcvtdq2ph %ymm2, %xmm1");
	asm volatile("vcvtpd2ph %zmm2, %xmm1");
	asm volatile("vcvtpd2ph %xmm2, %xmm1");
	asm volatile("vcvtpd2ph %ymm2, %xmm1");
	asm volatile("vcvtph2dq %ymm2, %zmm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2dq %xmm2, %xmm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2dq %xmm2, %ymm1");
	asm volatile("vcvtph2dq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2pd %xmm2, %zmm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2pd %xmm2, %xmm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2pd %xmm2, %ymm1");
	asm volatile("vcvtph2pd 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2ps %ymm2, %zmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2ps %xmm2, %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2ps %xmm2, %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2ps %xmm2, %xmm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2ps %xmm2, %ymm1");
	asm volatile("vcvtph2ps 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2psx %ymm2, %zmm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2psx %xmm2, %xmm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2psx %xmm2, %ymm1");
	asm volatile("vcvtph2psx 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2qq %xmm2, %zmm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2qq %xmm2, %xmm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2qq %xmm2, %ymm1");
	asm volatile("vcvtph2qq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2udq %ymm2, %zmm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2udq %xmm2, %xmm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2udq %xmm2, %ymm1");
	asm volatile("vcvtph2udq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2uqq %xmm2, %zmm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2uqq %xmm2, %xmm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2uqq %xmm2, %ymm1");
	asm volatile("vcvtph2uqq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2uw %zmm2, %zmm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2uw %xmm2, %xmm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2uw %ymm2, %ymm1");
	asm volatile("vcvtph2uw 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtph2w %zmm2, %zmm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtph2w %xmm2, %xmm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtph2w %ymm2, %ymm1");
	asm volatile("vcvtph2w 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtps2ph $0x12, %zmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %zmm2, %ymm1");
	asm volatile("vcvtps2ph $0x12, %ymm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %ymm2, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2ph $0x12, %xmm2, %xmm1");
	asm volatile("vcvtps2ph $0x12, %xmm2, 0x12345678(%eax,%ecx,8)");
	asm volatile("vcvtps2phx %zmm2, %ymm1");
	asm volatile("vcvtps2phx 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtps2phx %xmm2, %xmm1");
	asm volatile("vcvtps2phx %ymm2, %xmm1");
	asm volatile("vcvtqq2ph %zmm2, %xmm1");
	asm volatile("vcvtqq2ph %xmm2, %xmm1");
	asm volatile("vcvtqq2ph %ymm2, %xmm1");
	asm volatile("vcvtsd2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2sd 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2si 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvtsh2ss 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsh2usi %xmm1, %eax");
	asm volatile("vcvtsh2usi 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvtsi2sh %eax, %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtsi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtss2sh %xmm3, %xmm2, %xmm1");
	asm volatile("vcvtss2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvttph2dq %ymm2, %zmm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2dq %xmm2, %xmm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2dq %xmm2, %ymm1");
	asm volatile("vcvttph2dq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2qq %xmm2, %zmm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2qq %xmm2, %xmm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2qq %xmm2, %ymm1");
	asm volatile("vcvttph2qq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2udq %ymm2, %zmm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2udq %xmm2, %xmm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2udq %xmm2, %ymm1");
	asm volatile("vcvttph2udq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2uqq %xmm2, %zmm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2uqq %xmm2, %xmm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2uqq %xmm2, %ymm1");
	asm volatile("vcvttph2uqq 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2uw %zmm2, %zmm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2uw %xmm2, %xmm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2uw %ymm2, %ymm1");
	asm volatile("vcvttph2uw 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttph2w %zmm2, %zmm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvttph2w %xmm2, %xmm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvttph2w %ymm2, %ymm1");
	asm volatile("vcvttph2w 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvttsh2si %xmm1, %eax");
	asm volatile("vcvttsh2si 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvttsh2usi %xmm1, %eax");
	asm volatile("vcvttsh2usi 0x12345678(%eax,%ecx,8), %eax");
	asm volatile("vcvtudq2ph %zmm2, %ymm1");
	asm volatile("vcvtudq2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtudq2ph %xmm2, %xmm1");
	asm volatile("vcvtudq2ph %ymm2, %xmm1");
	asm volatile("vcvtuqq2ph %zmm2, %xmm1");
	asm volatile("vcvtuqq2ph %xmm2, %xmm1");
	asm volatile("vcvtuqq2ph %ymm2, %xmm1");
	asm volatile("vcvtusi2sh %eax, %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtusi2sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vcvtuw2ph %zmm2, %zmm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtuw2ph %xmm2, %xmm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtuw2ph %ymm2, %ymm1");
	asm volatile("vcvtuw2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vcvtw2ph %zmm2, %zmm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vcvtw2ph %xmm2, %xmm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vcvtw2ph %ymm2, %ymm1");
	asm volatile("vcvtw2ph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vdivph %zmm3, %zmm2, %zmm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vdivph %xmm3, %xmm2, %xmm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vdivph %ymm3, %ymm2, %ymm1");
	asm volatile("vdivph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vdivsh %xmm3, %xmm2, %xmm1");
	asm volatile("vdivsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfcmaddcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmaddcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfcmaddcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfcmaddcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmaddcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfcmulcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfcmulcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfcmulcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfcmulcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfcmulcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmadd231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmadd231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmaddsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmaddsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmaddsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmaddsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsub231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsub231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmsubadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmsubadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmsubadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmsubadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmulcph %zmm3, %zmm2, %zmm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfmulcph %xmm3, %xmm2, %xmm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfmulcph %ymm3, %ymm2, %ymm1");
	asm volatile("vfmulcph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfmulcsh %xmm3, %xmm2, %xmm1");
	asm volatile("vfmulcsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmadd231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmadd231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmadd231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmadd231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmadd231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub132ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub132ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub132ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub132sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub132sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub213ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub213ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub213ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub213sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub213sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231ph %zmm3, %zmm2, %zmm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vfnmsub231ph %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfnmsub231ph %ymm3, %ymm2, %ymm1");
	asm volatile("vfnmsub231ph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vfnmsub231sh %xmm3, %xmm2, %xmm1");
	asm volatile("vfnmsub231sh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vfpclassph $0x12, %zmm1, %k5");
	asm volatile("vfpclassph $0x12, %xmm1, %k5");
	asm volatile("vfpclassph $0x12, %ymm1, %k5");
	asm volatile("vfpclasssh $0x12, %xmm1, %k5");
	asm volatile("vfpclasssh $0x12, 0x12345678(%eax,%ecx,8), %k5");
	asm volatile("vgetexpph %zmm2, %zmm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vgetexpph %xmm2, %xmm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vgetexpph %ymm2, %ymm1");
	asm volatile("vgetexpph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vgetexpsh %xmm3, %xmm2, %xmm1");
	asm volatile("vgetexpsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vgetmantph $0x12, %zmm2, %zmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vgetmantph $0x12, %xmm2, %xmm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vgetmantph $0x12, %ymm2, %ymm1");
	asm volatile("vgetmantph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vgetmantsh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vgetmantsh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmaxph %zmm3, %zmm2, %zmm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vmaxph %xmm3, %xmm2, %xmm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmaxph %ymm3, %ymm2, %ymm1");
	asm volatile("vmaxph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vmaxsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmaxsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vminph %zmm3, %zmm2, %zmm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vminph %xmm3, %xmm2, %xmm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vminph %ymm3, %ymm2, %ymm1");
	asm volatile("vminph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vminsh %xmm3, %xmm2, %xmm1");
	asm volatile("vminsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmovsh %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vmovsh 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vmovsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmovw %xmm1, %eax");
	asm volatile("vmovw %xmm1, 0x12345678(%eax,%ecx,8)");
	asm volatile("vmovw %eax, %xmm1");
	asm volatile("vmovw 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vmulph %zmm3, %zmm2, %zmm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vmulph %xmm3, %xmm2, %xmm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vmulph %ymm3, %ymm2, %ymm1");
	asm volatile("vmulph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vmulsh %xmm3, %xmm2, %xmm1");
	asm volatile("vmulsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrcpph %zmm2, %zmm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrcpph %xmm2, %xmm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrcpph %ymm2, %ymm1");
	asm volatile("vrcpph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrcpsh %xmm3, %xmm2, %xmm1");
	asm volatile("vrcpsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vreduceph $0x12, %zmm2, %zmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vreduceph $0x12, %xmm2, %xmm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vreduceph $0x12, %ymm2, %ymm1");
	asm volatile("vreduceph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vreducesh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vreducesh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrndscaleph $0x12, %zmm2, %zmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrndscaleph $0x12, %xmm2, %xmm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrndscaleph $0x12, %ymm2, %ymm1");
	asm volatile("vrndscaleph $0x12, 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrndscalesh $0x12, %xmm3, %xmm2, %xmm1");
	asm volatile("vrndscalesh $0x12, 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vrsqrtph %zmm2, %zmm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vrsqrtph %xmm2, %xmm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vrsqrtph %ymm2, %ymm1");
	asm volatile("vrsqrtph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vrsqrtsh %xmm3, %xmm2, %xmm1");
	asm volatile("vrsqrtsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vscalefph %zmm3, %zmm2, %zmm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vscalefph %xmm3, %xmm2, %xmm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vscalefph %ymm3, %ymm2, %ymm1");
	asm volatile("vscalefph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vscalefsh %xmm3, %xmm2, %xmm1");
	asm volatile("vscalefsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsqrtph %zmm2, %zmm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %zmm1");
	asm volatile("vsqrtph %xmm2, %xmm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %xmm1");
	asm volatile("vsqrtph %ymm2, %ymm1");
	asm volatile("vsqrtph 0x12345678(%eax,%ecx,8), %ymm1");
	asm volatile("vsqrtsh %xmm3, %xmm2, %xmm1");
	asm volatile("vsqrtsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsubph %zmm3, %zmm2, %zmm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %zmm2, %zmm1");
	asm volatile("vsubph %xmm3, %xmm2, %xmm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vsubph %ymm3, %ymm2, %ymm1");
	asm volatile("vsubph 0x12345678(%eax,%ecx,8), %ymm2, %ymm1");
	asm volatile("vsubsh %xmm3, %xmm2, %xmm1");
	asm volatile("vsubsh 0x12345678(%eax,%ecx,8), %xmm2, %xmm1");
	asm volatile("vucomish %xmm2, %xmm1");
	asm volatile("vucomish 0x12345678(%eax,%ecx,8), %xmm1");

#endif /* #ifndef __x86_64__ */

	/* Key Locker */

	asm volatile("	loadiwkey %xmm1, %xmm2");
	asm volatile("	encodekey128 %eax, %edx");
	asm volatile("	encodekey256 %eax, %edx");
	asm volatile("	aesenc128kl 0x77(%edx), %xmm3");
	asm volatile("	aesenc256kl 0x77(%edx), %xmm3");
	asm volatile("	aesdec128kl 0x77(%edx), %xmm3");
	asm volatile("	aesdec256kl 0x77(%edx), %xmm3");
	asm volatile("	aesencwide128kl	0x77(%edx)");
	asm volatile("	aesencwide256kl	0x77(%edx)");
	asm volatile("	aesdecwide128kl	0x77(%edx)");
	asm volatile("	aesdecwide256kl	0x77(%edx)");

	/* Remote Atomic Operations */

	asm volatile("aadd %ecx,(%eax)");
	asm volatile("aadd %edx,(0x12345678)");
	asm volatile("aadd %edx,0x12345678(%eax,%ecx,8)");

	asm volatile("aand %ecx,(%eax)");
	asm volatile("aand %edx,(0x12345678)");
	asm volatile("aand %edx,0x12345678(%eax,%ecx,8)");

	asm volatile("aor %ecx,(%eax)");
	asm volatile("aor %edx,(0x12345678)");
	asm volatile("aor %edx,0x12345678(%eax,%ecx,8)");

	asm volatile("axor %ecx,(%eax)");
	asm volatile("axor %edx,(0x12345678)");
	asm volatile("axor %edx,0x12345678(%eax,%ecx,8)");

	/* AVX NE Convert */

	asm volatile("vbcstnebf162ps (%ecx),%xmm6");
	asm volatile("vbcstnesh2ps (%ecx),%xmm6");
	asm volatile("vcvtneebf162ps (%ecx),%xmm6");
	asm volatile("vcvtneeph2ps (%ecx),%xmm6");
	asm volatile("vcvtneobf162ps (%ecx),%xmm6");
	asm volatile("vcvtneoph2ps (%ecx),%xmm6");
	asm volatile("vcvtneps2bf16 %xmm1,%xmm6");

	/* AVX VNNI INT16 */

	asm volatile("vpdpbssd %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpbssds %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpbsud %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpbsuds %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpbuud %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpbuuds %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwsud %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwsuds %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwusd %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwusds %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwuud %xmm1,%xmm2,%xmm3");
	asm volatile("vpdpwuuds %xmm1,%xmm2,%xmm3");

	/* AVX IFMA */

	asm volatile("vpmadd52huq %xmm1,%xmm2,%xmm3");
	asm volatile("vpmadd52luq %xmm1,%xmm2,%xmm3");

	/* AVX SHA512 */

	asm volatile("vsha512msg1 %xmm1,%ymm2");
	asm volatile("vsha512msg2 %ymm1,%ymm2");
	asm volatile("vsha512rnds2 %xmm1,%ymm2,%ymm3");

	/* AVX SM3 */

	asm volatile("vsm3msg1 %xmm1,%xmm2,%xmm3");
	asm volatile("vsm3msg2 %xmm1,%xmm2,%xmm3");
	asm volatile("vsm3rnds2 $0xa1,%xmm1,%xmm2,%xmm3");

	/* AVX SM4 */

	asm volatile("vsm4key4 %xmm1,%xmm2,%xmm3");
	asm volatile("vsm4rnds4 %xmm1,%xmm2,%xmm3");

	/* Pre-fetch */

	asm volatile("prefetch (%eax)");
	asm volatile("prefetcht0 (%eax)");
	asm volatile("prefetcht1 (%eax)");
	asm volatile("prefetcht2 (%eax)");
	asm volatile("prefetchnta (%eax)");

	/* Non-serializing write MSR */

	asm volatile("wrmsrns");

	/* Prediction history reset */

	asm volatile("hreset $0");

	/* Serialize instruction execution */

	asm volatile("serialize");

	/* TSX suspend load address tracking */

	asm volatile("xresldtrk");
	asm volatile("xsusldtrk");

	/* SGX */

	asm volatile("encls");
	asm volatile("enclu");
	asm volatile("enclv");

	/* pconfig */

	asm volatile("pconfig");

	/* wbnoinvd */

	asm volatile("wbnoinvd");

	/* Following line is a marker for the awk script - do not change */
	asm volatile("rdtsc"); /* Stop here */

	return 0;
}
