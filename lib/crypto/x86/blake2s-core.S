/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2017-2019 Samuel Neves <sneves@dei.uc.pt>. All Rights Reserved.
 */

#include <linux/linkage.h>

.section .rodata.cst32.iv, "aM", @progbits, 32
.align 32
.Liv:
	.octa 0xA54FF53A3C6EF372BB67AE856A09E667
	.octa 0x5BE0CD191F83D9AB9B05688C510E527F

.section .rodata.cst16.ror16, "aM", @progbits, 16
.align 16
.Lror16:
	.octa 0x0D0C0F0E09080B0A0504070601000302

.section .rodata.cst16.ror8, "aM", @progbits, 16
.align 16
.Lror8:
	.octa 0x0C0F0E0D080B0A090407060500030201

.section .rodata.cst64.sigma, "aM", @progbits, 160
.align 64
.Lsigma:
.byte  0,  2,  4,  6,  1,  3,  5,  7, 14,  8, 10, 12, 15,  9, 11, 13
.byte 14,  4,  9, 13, 10,  8, 15,  6,  5,  1,  0, 11,  3, 12,  2,  7
.byte 11, 12,  5, 15,  8,  0,  2, 13,  9, 10,  3,  7,  4, 14,  6,  1
.byte  7,  3, 13, 11,  9,  1, 12, 14, 15,  2,  5,  4,  8,  6, 10,  0
.byte  9,  5,  2, 10,  0,  7,  4, 15,  3, 14, 11,  6, 13,  1, 12,  8
.byte  2,  6,  0,  8, 12, 10, 11,  3,  1,  4,  7, 15,  9, 13,  5, 14
.byte 12,  1, 14,  4,  5, 15, 13, 10,  8,  0,  6,  9, 11,  7,  3,  2
.byte 13,  7, 12,  3, 11, 14,  1,  9,  2,  5, 15,  8, 10,  0,  4,  6
.byte  6, 14, 11,  0, 15,  9,  3,  8, 10, 12, 13,  1,  5,  2,  7,  4
.byte 10,  8,  7,  1,  2,  4,  6,  5, 13, 15,  9,  3,  0, 11, 14, 12

.section .rodata.cst64.sigma2, "aM", @progbits, 160
.align 64
.Lsigma2:
.byte  0,  2,  4,  6,  1,  3,  5,  7, 14,  8, 10, 12, 15,  9, 11, 13
.byte  8,  2, 13, 15, 10,  9, 12,  3,  6,  4,  0, 14,  5, 11,  1,  7
.byte 11, 13,  8,  6,  5, 10, 14,  3,  2,  4, 12, 15,  1,  0,  7,  9
.byte 11, 10,  7,  0,  8, 15,  1, 13,  3,  6,  2, 12,  4, 14,  9,  5
.byte  4, 10,  9, 14, 15,  0, 11,  8,  1,  7,  3, 13,  2,  5,  6, 12
.byte  2, 11,  4, 15, 14,  3, 10,  8, 13,  6,  5,  7,  0, 12,  1,  9
.byte  4,  8, 15,  9, 14, 11, 13,  5,  3,  2,  1, 12,  6, 10,  7,  0
.byte  6, 13,  0, 14, 12,  2,  1, 11, 15,  4,  5,  8,  7,  9,  3, 10
.byte 15,  5,  4, 13, 10,  7,  3, 11, 12,  2,  0,  6,  9,  8,  1, 14
.byte  8,  7, 14, 11, 13, 15,  0, 12, 10,  4,  5,  6,  3,  2,  1,  9

#define CTX		%rdi
#define DATA		%rsi
#define NBLOCKS		%rdx
#define INC		%ecx

.text
//
// void blake2s_compress_ssse3(struct blake2s_ctx *ctx,
//			       const u8 *data, size_t nblocks, u32 inc);
//
// Only the first three fields of struct blake2s_ctx are used:
//	u32 h[8];	(inout)
//	u32 t[2];	(inout)
//	u32 f[2];	(in)
//
SYM_FUNC_START(blake2s_compress_ssse3)
	movdqu		(CTX),%xmm0		// Load h[0..3]
	movdqu		16(CTX),%xmm1		// Load h[4..7]
	movdqa		.Lror16(%rip),%xmm12
	movdqa		.Lror8(%rip),%xmm13
	movdqu		32(CTX),%xmm14		// Load t and f
	movd		INC,%xmm15		// Load inc
	leaq		.Lsigma+160(%rip),%r8
	jmp		.Lssse3_mainloop

	.align		32
.Lssse3_mainloop:
	// Main loop: each iteration processes one 64-byte block.
	movdqa		%xmm0,%xmm10		// Save h[0..3] and let v[0..3] = h[0..3]
	movdqa		%xmm1,%xmm11		// Save h[4..7] and let v[4..7] = h[4..7]
	paddq		%xmm15,%xmm14		// t += inc (64-bit addition)
	movdqa		.Liv(%rip),%xmm2	// v[8..11] = iv[0..3]
	movdqa		%xmm14,%xmm3
	pxor		.Liv+16(%rip),%xmm3	// v[12..15] = iv[4..7] ^ [t, f]
	leaq		.Lsigma(%rip),%rcx

.Lssse3_roundloop:
	// Round loop: each iteration does 1 round (of 10 rounds total).
	movzbl		(%rcx),%eax
	movd		(DATA,%rax,4),%xmm4
	movzbl		1(%rcx),%eax
	movd		(DATA,%rax,4),%xmm5
	movzbl		2(%rcx),%eax
	movd		(DATA,%rax,4),%xmm6
	movzbl		3(%rcx),%eax
	movd		(DATA,%rax,4),%xmm7
	punpckldq	%xmm5,%xmm4
	punpckldq	%xmm7,%xmm6
	punpcklqdq	%xmm6,%xmm4
	paddd		%xmm4,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm12,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$12,%xmm1
	pslld		$20,%xmm8
	por		%xmm8,%xmm1
	movzbl		4(%rcx),%eax
	movd		(DATA,%rax,4),%xmm5
	movzbl		5(%rcx),%eax
	movd		(DATA,%rax,4),%xmm6
	movzbl		6(%rcx),%eax
	movd		(DATA,%rax,4),%xmm7
	movzbl		7(%rcx),%eax
	movd		(DATA,%rax,4),%xmm4
	punpckldq	%xmm6,%xmm5
	punpckldq	%xmm4,%xmm7
	punpcklqdq	%xmm7,%xmm5
	paddd		%xmm5,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm13,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$7,%xmm1
	pslld		$25,%xmm8
	por		%xmm8,%xmm1
	pshufd		$0x93,%xmm0,%xmm0
	pshufd		$0x4e,%xmm3,%xmm3
	pshufd		$0x39,%xmm2,%xmm2
	movzbl		8(%rcx),%eax
	movd		(DATA,%rax,4),%xmm6
	movzbl		9(%rcx),%eax
	movd		(DATA,%rax,4),%xmm7
	movzbl		10(%rcx),%eax
	movd		(DATA,%rax,4),%xmm4
	movzbl		11(%rcx),%eax
	movd		(DATA,%rax,4),%xmm5
	punpckldq	%xmm7,%xmm6
	punpckldq	%xmm5,%xmm4
	punpcklqdq	%xmm4,%xmm6
	paddd		%xmm6,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm12,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$12,%xmm1
	pslld		$20,%xmm8
	por		%xmm8,%xmm1
	movzbl		12(%rcx),%eax
	movd		(DATA,%rax,4),%xmm7
	movzbl		13(%rcx),%eax
	movd		(DATA,%rax,4),%xmm4
	movzbl		14(%rcx),%eax
	movd		(DATA,%rax,4),%xmm5
	movzbl		15(%rcx),%eax
	movd		(DATA,%rax,4),%xmm6
	punpckldq	%xmm4,%xmm7
	punpckldq	%xmm6,%xmm5
	punpcklqdq	%xmm5,%xmm7
	paddd		%xmm7,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm13,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$7,%xmm1
	pslld		$25,%xmm8
	por		%xmm8,%xmm1
	pshufd		$0x39,%xmm0,%xmm0
	pshufd		$0x4e,%xmm3,%xmm3
	pshufd		$0x93,%xmm2,%xmm2
	addq		$16,%rcx
	cmpq		%r8,%rcx
	jnz		.Lssse3_roundloop

	// Compute the new h: h[0..7] ^= v[0..7] ^ v[8..15]
	pxor		%xmm2,%xmm0
	pxor		%xmm3,%xmm1
	pxor		%xmm10,%xmm0
	pxor		%xmm11,%xmm1
	addq		$64,DATA
	decq		NBLOCKS
	jnz		.Lssse3_mainloop

	movdqu		%xmm0,(CTX)		// Store new h[0..3]
	movdqu		%xmm1,16(CTX)		// Store new h[4..7]
	movq		%xmm14,32(CTX)		// Store new t (f is unchanged)
	RET
SYM_FUNC_END(blake2s_compress_ssse3)

//
// void blake2s_compress_avx512(struct blake2s_ctx *ctx,
//				const u8 *data, size_t nblocks, u32 inc);
//
// Only the first three fields of struct blake2s_ctx are used:
//	u32 h[8];	(inout)
//	u32 t[2];	(inout)
//	u32 f[2];	(in)
//
SYM_FUNC_START(blake2s_compress_avx512)
	vmovdqu		(CTX),%xmm0		// Load h[0..3]
	vmovdqu		16(CTX),%xmm1		// Load h[4..7]
	vmovdqu		32(CTX),%xmm4		// Load t and f
	vmovd		INC,%xmm5		// Load inc
	vmovdqa		.Liv(%rip),%xmm14	// Load iv[0..3]
	vmovdqa		.Liv+16(%rip),%xmm15	// Load iv[4..7]
	jmp		.Lavx512_mainloop

	.align		32
.Lavx512_mainloop:
	// Main loop: each iteration processes one 64-byte block.
	vmovdqa		%xmm0,%xmm10		// Save h[0..3] and let v[0..3] = h[0..3]
	vmovdqa		%xmm1,%xmm11		// Save h[4..7] and let v[4..7] = h[4..7]
	vpaddq		%xmm5,%xmm4,%xmm4	// t += inc (64-bit addition)
	vmovdqa		%xmm14,%xmm2		// v[8..11] = iv[0..3]
	vpxor		%xmm15,%xmm4,%xmm3	// v[12..15] = iv[4..7] ^ [t, f]
	vmovdqu		(DATA),%ymm6		// Load first 8 data words
	vmovdqu		32(DATA),%ymm7		// Load second 8 data words
	addq		$64,DATA
	leaq		.Lsigma2(%rip),%rax
	movb		$10,%cl			// Set num rounds remaining

.Lavx512_roundloop:
	// Round loop: each iteration does 1 round (of 10 rounds total).
	vpmovzxbd	(%rax),%ymm8
	vpmovzxbd	8(%rax),%ymm9
	addq		$16,%rax
	vpermi2d	%ymm7,%ymm6,%ymm8
	vpermi2d	%ymm7,%ymm6,%ymm9
	vmovdqa		%ymm8,%ymm6
	vmovdqa		%ymm9,%ymm7
	vpaddd		%xmm8,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$16,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$12,%xmm1,%xmm1
	vextracti128	$1,%ymm8,%xmm8
	vpaddd		%xmm8,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$8,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$7,%xmm1,%xmm1
	vpshufd		$0x93,%xmm0,%xmm0
	vpshufd		$0x4e,%xmm3,%xmm3
	vpshufd		$0x39,%xmm2,%xmm2
	vpaddd		%xmm9,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$16,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$12,%xmm1,%xmm1
	vextracti128	$1,%ymm9,%xmm9
	vpaddd		%xmm9,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$8,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$7,%xmm1,%xmm1
	vpshufd		$0x39,%xmm0,%xmm0
	vpshufd		$0x4e,%xmm3,%xmm3
	vpshufd		$0x93,%xmm2,%xmm2
	decb		%cl
	jne		.Lavx512_roundloop

	// Compute the new h: h[0..7] ^= v[0..7] ^ v[8..15]
	vpternlogd	$0x96,%xmm10,%xmm2,%xmm0
	vpternlogd	$0x96,%xmm11,%xmm3,%xmm1
	decq		NBLOCKS
	jne		.Lavx512_mainloop

	vmovdqu		%xmm0,(CTX)		// Store new h[0..3]
	vmovdqu		%xmm1,16(CTX)		// Store new h[4..7]
	vmovq		%xmm4,32(CTX)		// Store new t (f is unchanged)
	vzeroupper
	RET
SYM_FUNC_END(blake2s_compress_avx512)
