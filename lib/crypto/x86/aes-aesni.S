/* SPDX-License-Identifier: GPL-2.0-or-later */
//
// AES block cipher using AES-NI instructions
//
// Copyright 2026 Google LLC
//
// The code in this file supports 32-bit and 64-bit CPUs, and it doesn't require
// AVX.  It does use up to SSE4.1, which all CPUs with AES-NI have.
#include <linux/linkage.h>

.section .rodata
#ifdef __x86_64__
#define RODATA(label)	label(%rip)
#else
#define RODATA(label)	label
#endif

	// A mask for pshufb that extracts the last dword, rotates it right by 8
	// bits, and copies the result to all four dwords.
.p2align 4
.Lmask:
	.byte	13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12

	// The AES round constants, used during key expansion
.Lrcon:
	.long	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36

.text

// Transform four dwords [a0, a1, a2, a3] in \a into
// [a0, a0^a1, a0^a1^a2, a0^a1^a2^a3].  \tmp is a temporary xmm register.
//
// Note: this could be done in four instructions, shufps + pxor + shufps + pxor,
// if the temporary register were zero-initialized ahead of time.  We instead do
// it in an easier-to-understand way that doesn't require zero-initialization
// and avoids the unusual shufps instruction.  movdqa is usually "free" anyway.
.macro	_prefix_sum	a, tmp
	movdqa		\a, \tmp	// [a0, a1, a2, a3]
	pslldq		$4, \a		// [0, a0, a1, a2]
	pxor		\tmp, \a	// [a0, a0^a1, a1^a2, a2^a3]
	movdqa		\a, \tmp
	pslldq		$8, \a		// [0, 0, a0, a0^a1]
	pxor		\tmp, \a	// [a0, a0^a1, a0^a1^a2, a0^a1^a2^a3]
.endm

.macro	_gen_round_key	a, b
	// Compute four copies of rcon[i] ^ SubBytes(ror32(w, 8)), where w is
	// the last dword of the previous round key (given in \b).
	//
	// 'aesenclast src, dst' does dst = src XOR SubBytes(ShiftRows(dst)).
	// It is used here solely for the SubBytes and the XOR.  The ShiftRows
	// is a no-op because all four columns are the same here.
	//
	// Don't use the 'aeskeygenassist' instruction, since:
	//  - On most Intel CPUs it is microcoded, making it have a much higher
	//    latency and use more execution ports than 'aesenclast'.
	//  - It cannot be used in a loop, since it requires an immediate.
	//  - It doesn't do much more than 'aesenclast' in the first place.
	movdqa		\b, %xmm2
	pshufb		MASK, %xmm2
	aesenclast	RCON, %xmm2

	// XOR in the prefix sum of the four dwords of \a, which is the
	// previous round key (AES-128) or the first round key in the previous
	// pair of round keys (AES-256).  The result is the next round key.
	_prefix_sum	\a, tmp=%xmm3
	pxor		%xmm2, \a

	// Store the next round key to memory.  Also leave it in \a.
	movdqu		\a, (RNDKEYS)
.endm

.macro	_aes_expandkey_aesni	is_aes128
#ifdef __x86_64__
	// Arguments
	.set	RNDKEYS,	%rdi
	.set	INV_RNDKEYS,	%rsi
	.set	IN_KEY,		%rdx

	// Other local variables
	.set	RCON_PTR,	%rcx
	.set	COUNTER,	%eax
#else
	// Arguments, assuming -mregparm=3
	.set	RNDKEYS,	%eax
	.set	INV_RNDKEYS,	%edx
	.set	IN_KEY,		%ecx

	// Other local variables
	.set	RCON_PTR,	%ebx
	.set	COUNTER,	%esi
#endif
	.set	RCON,		%xmm6
	.set	MASK,		%xmm7

#ifdef __i386__
	push		%ebx
	push		%esi
#endif

.if \is_aes128
	// AES-128: the first round key is simply a copy of the raw key.
	movdqu		(IN_KEY), %xmm0
	movdqu		%xmm0, (RNDKEYS)
.else
	// AES-256: the first two round keys are simply a copy of the raw key.
	movdqu		(IN_KEY), %xmm0
	movdqu		%xmm0, (RNDKEYS)
	movdqu		16(IN_KEY), %xmm1
	movdqu		%xmm1, 16(RNDKEYS)
	add		$32, RNDKEYS
.endif

	// Generate the remaining round keys.
	movdqa		RODATA(.Lmask), MASK
.if \is_aes128
	lea		RODATA(.Lrcon), RCON_PTR
	mov		$10, COUNTER
.Lgen_next_aes128_round_key:
	add		$16, RNDKEYS
	movd		(RCON_PTR), RCON
	pshufd		$0x00, RCON, RCON
	add		$4, RCON_PTR
	_gen_round_key	%xmm0, %xmm0
	dec		COUNTER
	jnz		.Lgen_next_aes128_round_key
.else
	// AES-256: only the first 7 round constants are needed, so instead of
	// loading each one from memory, just start by loading [1, 1, 1, 1] and
	// then generate the rest by doubling.
	pshufd		$0x00, RODATA(.Lrcon), RCON
	pxor		%xmm5, %xmm5	// All-zeroes
	mov		$7, COUNTER
.Lgen_next_aes256_round_key_pair:
	// Generate the next AES-256 round key: either the first of a pair of
	// two, or the last one.
	_gen_round_key	%xmm0, %xmm1

	dec		COUNTER
	jz		.Lgen_aes256_round_keys_done

	// Generate the second AES-256 round key of the pair.  Compared to the
	// first, there's no rotation and no XOR of a round constant.
	pshufd		$0xff, %xmm0, %xmm2	// Get four copies of last dword
	aesenclast	%xmm5, %xmm2		// Just does SubBytes
	_prefix_sum	%xmm1, tmp=%xmm3
	pxor		%xmm2, %xmm1
	movdqu		%xmm1, 16(RNDKEYS)
	add		$32, RNDKEYS
	paddd		RCON, RCON		// RCON <<= 1
	jmp		.Lgen_next_aes256_round_key_pair
.Lgen_aes256_round_keys_done:
.endif

	// If INV_RNDKEYS is non-NULL, write the round keys for the Equivalent
	// Inverse Cipher to it.  To do that, reverse the standard round keys,
	// and apply aesimc (InvMixColumn) to each except the first and last.
	test		INV_RNDKEYS, INV_RNDKEYS
	jz		.Ldone\@
	movdqu		(RNDKEYS), %xmm0	// Last standard round key
	movdqu		%xmm0, (INV_RNDKEYS)	// => First inverse round key
.if \is_aes128
	mov		$9, COUNTER
.else
	mov		$13, COUNTER
.endif
.Lgen_next_inv_round_key\@:
	sub		$16, RNDKEYS
	add		$16, INV_RNDKEYS
	movdqu		(RNDKEYS), %xmm0
	aesimc		%xmm0, %xmm0
	movdqu		%xmm0, (INV_RNDKEYS)
	dec		COUNTER
	jnz		.Lgen_next_inv_round_key\@
	movdqu		-16(RNDKEYS), %xmm0	// First standard round key
	movdqu		%xmm0, 16(INV_RNDKEYS)	// => Last inverse round key

.Ldone\@:
#ifdef __i386__
	pop		%esi
	pop		%ebx
#endif
	RET
.endm

// void aes128_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
//			       const u8 in_key[AES_KEYSIZE_128]);
SYM_FUNC_START(aes128_expandkey_aesni)
	_aes_expandkey_aesni	1
SYM_FUNC_END(aes128_expandkey_aesni)

// void aes256_expandkey_aesni(u32 rndkeys[], u32 *inv_rndkeys,
//			       const u8 in_key[AES_KEYSIZE_256]);
SYM_FUNC_START(aes256_expandkey_aesni)
	_aes_expandkey_aesni	0
SYM_FUNC_END(aes256_expandkey_aesni)

.macro	_aes_crypt_aesni	enc
#ifdef __x86_64__
	.set	RNDKEYS,	%rdi
	.set	NROUNDS,	%esi
	.set	OUT,		%rdx
	.set	IN,		%rcx
#else
	// Assuming -mregparm=3
	.set	RNDKEYS,	%eax
	.set	NROUNDS,	%edx
	.set	OUT,		%ecx
	.set	IN,		%ebx	// Passed on stack
#endif

#ifdef __i386__
	push		%ebx
	mov		8(%esp), %ebx
#endif

	// Zero-th round
	movdqu		(IN), %xmm0
	movdqu		(RNDKEYS), %xmm1
	pxor		%xmm1, %xmm0

	// Normal rounds
	add		$16, RNDKEYS
	dec		NROUNDS
.Lnext_round\@:
	movdqu		(RNDKEYS), %xmm1
.if \enc
	aesenc		%xmm1, %xmm0
.else
	aesdec		%xmm1, %xmm0
.endif
	add		$16, RNDKEYS
	dec		NROUNDS
	jne		.Lnext_round\@

	// Last round
	movdqu		(RNDKEYS), %xmm1
.if \enc
	aesenclast	%xmm1, %xmm0
.else
	aesdeclast	%xmm1, %xmm0
.endif
	movdqu		%xmm0, (OUT)

#ifdef __i386__
	pop		%ebx
#endif
	RET
.endm

// void aes_encrypt_aesni(const u32 rndkeys[], int nrounds,
//			  u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
SYM_FUNC_START(aes_encrypt_aesni)
	_aes_crypt_aesni	1
SYM_FUNC_END(aes_encrypt_aesni)

// void aes_decrypt_aesni(const u32 inv_rndkeys[], int nrounds,
//			  u8 out[AES_BLOCK_SIZE], const u8 in[AES_BLOCK_SIZE]);
SYM_FUNC_START(aes_decrypt_aesni)
	_aes_crypt_aesni	0
SYM_FUNC_END(aes_decrypt_aesni)
