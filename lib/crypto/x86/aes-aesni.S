/* SPDX-License-Identifier: GPL-2.0-or-later */
//
// AES block cipher using AES-NI instructions
//
// Copyright 2026 Google LLC
//
// The code in this file supports 32-bit and 64-bit CPUs (except for
// aes_ctr64_crypt_aesni() which supports 64-bit only), and it doesn't require
// AVX.  It does use up to SSE4.1, which all CPUs with AES-NI have.
#include <linux/linkage.h>

#ifdef __x86_64__
#define RODATA(label)	label(%rip)

#define ARG0		%rdi
#define ARG1		%rsi
#define ARG2		%rdx
#define ARG2_32		%edx
#define ARG3		%rcx
#define ARG4		%r8
#define TMP		%rax
#define TMP_32		%eax
#define TMP_16		%ax
#define TMP_8		%al

#else // __x86_64__

#define RODATA(label)	label

// Caller-save GPRs and the first 3 function arguments, assuming -mregparm=3
#define ARG0		%eax
#define ARG1		%edx
#define ARG2		%ecx
#define ARG2_32		%ecx

// *Callee*-save GPRs.
#define ARG3		%edi
#define ARG4		%esi
#define TMP		%ebx
#define TMP_32		%ebx
#define TMP_16		%bx
#define TMP_8		%bl
#endif // !__x86_64__

// Offsets in struct aes_key
#define OFFSETOF_NROUNDS	4
#define OFFSETOF_ROUNDKEYS	16
#define OFFSETOF_INVROUNDKEYS	256

.section .rodata
.p2align 4
.Lxts_gf_poly:
	// For XTS: a constant used when advancing the tweak by one block by
	// multiplying by the polynomial 'x' in GF(2^128).  The low 64 bits of
	// this value represent the polynomial x^7 + x^2 + x + 1; it is the
	// value that must be XOR'd into the low 64 bits of the tweak each time
	// a 1 is carried out of the high 64 bits.
	//
	// The high 64 bits of this value is just the internal carry bit that
	// exists when there's a carry out of the low 64 bits of the tweak.
	.quad	0x87, 1

#ifdef __x86_64__
.Lbswap_mask:
	// A mask for pshufb that byte-reflects the value.
	.byte	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#endif

.Lexpandkey_mask:
	// A mask for pshufb that extracts the last dword, rotates it right by 8
	// bits, and copies the result to all four dwords.
	.byte	13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12

.Lrcon:
	// The AES round constants, used during key expansion
	.long	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36

.Lcts_permute_table:
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80

.text

// In 32-bit mode, push certain callee-saved GPRs and optionally load function
// arguments from the stack into them.  Do nothing in 64-bit mode.
//
// TMP is always made available as a temporary register.
//
// \uses_arg3 is 0 to not touch ARG3, 1 to make it available as a temporary
// register only, or 2 to actually load it as an argument from the stack.
// Likewise for \uses_arg4 and ARG4.  It's assumed the -mregparm=3 convention is
// being used and %esp hasn't been modified since function entry.
.macro _prologue	uses_arg3=0, uses_arg4=0
#ifdef __i386__
	.set	ARG3_OFFSET, 4	// Assuming -mregparm=3
	.set	ARG3_PUSHED, \uses_arg3
	.set	ARG4_PUSHED, \uses_arg4
.if \uses_arg3
	push	ARG3
	.set	ARG3_OFFSET, ARG3_OFFSET + 4
  .if \uses_arg3 == 2
	mov	ARG3_OFFSET(%esp), ARG3
  .endif
.endif
.if \uses_arg4
	push	ARG4
	.set	ARG3_OFFSET, ARG3_OFFSET + 4
  .if \uses_arg4 == 2
	mov	ARG3_OFFSET+4(%esp), ARG4
  .endif
.endif
	push	TMP
	.set	ARG3_OFFSET, ARG3_OFFSET + 4
#endif // __i386__
.endm

.macro _reload_arg3
#ifdef __i386__
	mov	ARG3_OFFSET(%esp), ARG3
#endif
.endm

// Undo any pushes that _prologue did, then return.
.macro _epilogue
#ifdef __i386__
	pop	TMP
.if ARG4_PUSHED
	pop	ARG4
.endif
.if ARG3_PUSHED
	pop	ARG3
.endif
#endif
	RET
.endm

.macro	_aesenc		enc, rndkey, data
.if \enc
	aesenc		\rndkey, \data
.else
	aesdec		\rndkey, \data
.endif
.endm

.macro	_aesenclast	enc, rndkeylast, data
.if \enc
	aesenclast	\rndkeylast, \data
.else
	aesdeclast	\rndkeylast, \data
.endif
.endm

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
	// Arguments
	.set	RNDKEYS,	ARG0
	.set	INV_RNDKEYS,	ARG1
	.set	IN_KEY,		ARG2

	// Other local variables
	.set	RCON_PTR,	ARG3
	.set	COUNTER,	TMP_32
	.set	RCON,		%xmm6
	.set	MASK,		%xmm7

	_prologue	uses_arg3=1

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
	movdqa		RODATA(.Lexpandkey_mask), MASK
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
	_epilogue
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

// AES-encrypt (\enc=1) or AES-decrypt (\enc=0) the AESDATA registers specified
// in \vecs using the aes_enckey or aes_key pointed to by KEY.  RNDKEY must be
// set to a temporary XMM register, NROUNDS to a temporary 32-bit GPR, and
// RNDKEY_PTR to a temporary full-size GPR.
.macro	_do_aes		enc, vecs:vararg
	movl		OFFSETOF_NROUNDS(KEY), NROUNDS
	dec		NROUNDS
.if \enc
	.set		rndkey0_offs, OFFSETOF_ROUNDKEYS
.else
	.set		rndkey0_offs, OFFSETOF_INVROUNDKEYS
.endif

	// Do the zero-th AES round.
	movdqu		rndkey0_offs(KEY), RNDKEY
.irp i, \vecs
	pxor		RNDKEY, AESDATA\i
.endr
	// Do the regular AES rounds.
	lea		rndkey0_offs+16(KEY), RNDKEY_PTR
.Lnext_round\@:
	movdqu		(RNDKEY_PTR), RNDKEY
	add		$16, RNDKEY_PTR
.irp i, \vecs
	_aesenc		\enc, RNDKEY, AESDATA\i
.endr
	dec		NROUNDS
	jnz		.Lnext_round\@
	// Do the last AES round.
	movdqu		(RNDKEY_PTR), RNDKEY
.irp i, \vecs
	_aesenclast	\enc, RNDKEY, AESDATA\i
.endr
.endm

.macro	_do_aes_ecb	enc, vecs:vararg
.irp i, \vecs
	movdqu		\i*16(SRC), AESDATA\i
.endr
	_do_aes		\enc, \vecs
.irp i, \vecs
	movdqu		AESDATA\i, \i*16(DST)
.endr
.endm

.macro	_aes_crypt_aesni	enc
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	KEY,		ARG2
	.set	RNDKEY_PTR,	ARG3	// Temporary register for _do_aes
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	AESDATA0,	%xmm0
	.set	RNDKEY,		%xmm1	// Temporary register for _do_aes

	_prologue	uses_arg3=1
	_do_aes_ecb	\enc, 0
	_epilogue
.endm

// void aes_encrypt_aesni(u8 dst[AES_BLOCK_SIZE], const u8 src[AES_BLOCK_SIZE],
//			  const struct aes_enckey *key);
SYM_FUNC_START(aes_encrypt_aesni)
	_aes_crypt_aesni	1
SYM_FUNC_END(aes_encrypt_aesni)

// void aes_decrypt_aesni(u8 dst[AES_BLOCK_SIZE], const u8 src[AES_BLOCK_SIZE],
//			  const struct aes_key *key);
SYM_FUNC_START(aes_decrypt_aesni)
	_aes_crypt_aesni	0
SYM_FUNC_END(aes_decrypt_aesni)

.macro	_ecb_crypt	enc
	// Arguments
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	NBLOCKS,	ARG2
	.set	NBLOCKS32,	ARG2_32	// Used for improved code density
	.set	KEY,		ARG3

	// Other local variables
	.set	RNDKEY_PTR,	ARG4	// Temporary register for _do_aes
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	AESDATA0,	%xmm0
	.set	AESDATA1,	%xmm1
	.set	AESDATA2,	%xmm2
	.set	AESDATA3,	%xmm3
	.set	RNDKEY,		%xmm4	// Temporary register for _do_aes
	_prologue	uses_arg3=2, uses_arg4=1

	sub		$4, NBLOCKS
	jl		.Lecb_loop4_done\@
.p2align 5
.Lecb_loop4\@:
	_do_aes_ecb	\enc, 0,1,2,3
	add		$64, SRC
	add		$64, DST
	sub		$4, NBLOCKS
	jge		.Lecb_loop4\@
.Lecb_loop4_done\@:
	add		$4, NBLOCKS32
	jz		.Lecb_done\@

.Lecb_loop1\@:
	_do_aes_ecb	\enc, 0
	add		$16, SRC
	add		$16, DST
	dec		NBLOCKS32
	jnz		.Lecb_loop1\@

.Lecb_done\@:
	_epilogue
.endm

// void aes_ecb_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      const struct aes_enckey *key);
SYM_FUNC_START(aes_ecb_encrypt_aesni)
	_ecb_crypt	1
SYM_FUNC_END(aes_ecb_encrypt_aesni)

// void aes_ecb_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      const struct aes_key *key);
SYM_FUNC_START(aes_ecb_decrypt_aesni)
	_ecb_crypt	0
SYM_FUNC_END(aes_ecb_decrypt_aesni)

// void aes_cbc_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      u8 iv[AES_BLOCK_SIZE],
//			      const struct aes_enckey *key);
//
// This assumes nblocks > 0.
SYM_FUNC_START(aes_cbc_encrypt_aesni)
	// Arguments
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	NBLOCKS,	ARG2
	.set	IV_PTR,		ARG3
	.set	KEY,		ARG4

	// Other local variables
#ifdef __x86_64__
	.set	RNDKEY_PTR,	%r9	// Temporary register for _do_aes
#else
	.set	RNDKEY_PTR,	IV_PTR	// IV_PTR is clobbered and reloaded later
#endif
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	AESDATA0,	%xmm0
	.set	PTEXT,		%xmm1
	.set	RNDKEY,		%xmm2	// Temporary register for _do_aes
	_prologue	uses_arg3=2, uses_arg4=2

	movdqu		(IV_PTR), AESDATA0
.p2align 5
.Lcbc_enc_loop:
	movdqu		(SRC), PTEXT
	pxor		PTEXT, AESDATA0
	_do_aes		1, 0
	movdqu		AESDATA0, (DST)
	add		$16, SRC
	add		$16, DST
	dec		NBLOCKS
	jnz		.Lcbc_enc_loop

	// Store the next IV.  On 32-bit, reload IV_PTR from stack first.
	_reload_arg3
	movdqu		AESDATA0, (IV_PTR)
	_epilogue
SYM_FUNC_END(aes_cbc_encrypt_aesni)

// void aes_cbc_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      u8 iv[AES_BLOCK_SIZE], const struct aes_key *key);
SYM_FUNC_START(aes_cbc_decrypt_aesni)
	// Arguments
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	NBLOCKS,	ARG2
	.set	NBLOCKS32,	ARG2_32	// Used for improved code density
	.set	IV_PTR,		ARG3
	.set	KEY,		ARG4

	// Other local variables
#ifdef __x86_64__
	.set	RNDKEY_PTR,	%r9	// Temporary register for _do_aes
#else
	.set	RNDKEY_PTR,	IV_PTR	// IV_PTR is clobbered and reloaded later
#endif
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	AESDATA0,	%xmm0
	.set	AESDATA1,	%xmm1
	.set	AESDATA2,	%xmm2
	.set	AESDATA3,	%xmm3
	.set	RNDKEY,		%xmm4	// Temporary register for _do_aes
	.set	IV,		%xmm5
	.set	CTEXT0,		%xmm6
	.set	CTEXT1,		%xmm7
#ifdef __x86_64__
	.set	CTEXT2,		%xmm8
	.set	CTEXT3,		%xmm9
#endif
	_prologue	uses_arg3=2, uses_arg4=2

	movdqu		(IV_PTR), IV

	sub		$4, NBLOCKS
	jl		.Lcbc_dec_loop4_done
.p2align 5
.Lcbc_dec_loop4:
	movdqu		0(SRC), AESDATA0
	movdqu		16(SRC), AESDATA1
	movdqu		32(SRC), AESDATA2
	movdqu		48(SRC), AESDATA3
	movdqa		AESDATA0, CTEXT0
	movdqa		AESDATA1, CTEXT1
#ifdef __x86_64__
	movdqa		AESDATA2, CTEXT2
	movdqa		AESDATA3, CTEXT3
#endif
	_do_aes		0, 0,1,2,3
	pxor		IV, AESDATA0
	pxor		CTEXT0, AESDATA1
	pxor		CTEXT1, AESDATA2
#ifdef __x86_64__
	pxor		CTEXT2, AESDATA3
	movdqa		CTEXT3, IV
#else
	movdqu		32(SRC), CTEXT0
	pxor		CTEXT0, AESDATA3
	movdqu		48(SRC), IV
#endif
	movdqu		AESDATA0, 0(DST)
	movdqu		AESDATA1, 16(DST)
	movdqu		AESDATA2, 32(DST)
	movdqu		AESDATA3, 48(DST)
	add		$64, SRC
	add		$64, DST
	sub		$4, NBLOCKS
	jge		.Lcbc_dec_loop4
.Lcbc_dec_loop4_done:
	add		$4, NBLOCKS32
	jz		.Lcbc_dec_done

.Lcbc_dec_loop1:
	movdqu		(SRC), AESDATA0
	movdqa		AESDATA0, CTEXT0
	_do_aes		0, 0
	pxor		IV, AESDATA0
	movdqa		CTEXT0, IV
	movdqu		AESDATA0, (DST)
	add		$16, SRC
	add		$16, DST
	dec		NBLOCKS32
	jnz		.Lcbc_dec_loop1

.Lcbc_dec_done:
	// Store the next IV.  On 32-bit, reload IV_PTR from stack first.
	_reload_arg3
	movdqu		IV, (IV_PTR)
	_epilogue
SYM_FUNC_END(aes_cbc_decrypt_aesni)

.macro	_aes_cbc_cts_crypt	enc
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	PN_LEN,		ARG2
	.set	IV_PTR,		ARG3
	.set	KEY,		ARG4
	.set	RNDKEY_PTR,	IV_PTR	// Temporary register for _do_aes
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	RSHIFT_MASK,	%xmm0 // range(16-PN_LEN,16) + [0x80, 0x80, ...]
				      // Must be %xmm0 for pblendvb to work.
	.set	LSHIFT_MASK,	%xmm1 // [0x80, 0x80, ...] + range(PN_LEN)
	.set	AESDATA0,	%xmm2
	.set	AESDATA1,	%xmm3
	.set	RNDKEY,		%xmm4	// Temporary register for _do_aes
	.set	IV,		%xmm5

	_prologue	uses_arg3=2, uses_arg4=2

	// Load LSHIFT_MASK and RSHIFT_MASK.
	lea		RODATA(.Lcts_permute_table), TMP
	movdqu		(TMP,PN_LEN), LSHIFT_MASK
	sub		PN_LEN, TMP
	movdqu		32(TMP), RSHIFT_MASK

	// Load the IV, freeing up IV_PTR to reuse for RNDKEY_PTR.
	movdqu		(IV_PTR), IV

	// Load the second-from-last source block (P[n-1] or C[n-1]) and
	// AES-en/decrypt it.  In the encryption case, XOR it with the IV first.
	movdqu		(SRC), AESDATA0
.if \enc
	pxor		IV, AESDATA0
.endif
	_do_aes		\enc, 0

	// Load the last source block (P[n] or C[n]), left-aligned.
	movdqu		(SRC,PN_LEN), AESDATA1

.if \enc
	// Right-align P[n], XOR it with the first AES output, and AES-encrypt
	// the result.  This gives C[n-1] in AESDATA1.
	pshufb		RSHIFT_MASK, AESDATA1
	pxor		AESDATA0, AESDATA1
	_do_aes		1, 1

	// Left-align the first AES output to get left-aligned C[n].
	pshufb		LSHIFT_MASK, AESDATA0
.else
	// Save a copy of the first AES output in %xmm6 for later.
	movdqa		AESDATA0, %xmm6

	// Left-align the first AES output and XOR it with the
	// already-left-aligned C[n].  This gives the left-aligned P[n].
	pshufb		LSHIFT_MASK, AESDATA0
	pxor		AESDATA1, AESDATA0

	// Right-align C[n], then use RSHIFT_MASK (in %xmm0 as pblendvb's
	// implicit operand) as a blend mask to select C[n] and the remaining
	// bytes from the first AES output, with the result in AESDATA1.
	pshufb		RSHIFT_MASK, AESDATA1
	pblendvb	%xmm6, AESDATA1

	// AES-decrypt the result and XOR with the IV to get P[n-1] in AESDATA1.
	_do_aes		0, 1
	pxor		IV, AESDATA1
.endif
	// Store the left-aligned C[n] or P[n], then C[n-1] or P[n-1].
	// Note that these stores may overlap.
	movdqu		AESDATA0, (DST,PN_LEN)
	movdqu		AESDATA1, (DST)
	_epilogue
.endm

// void aes_cbc_cts_encrypt_aesni(u8 *dst, const u8 *src, long pn_len,
//				  const u8 iv[AES_BLOCK_SIZE],
//				  const struct aes_enckey *key);
// void aes_cbc_cts_decrypt_aesni(u8 *dst, const u8 *src, long pn_len,
//				  const u8 iv[AES_BLOCK_SIZE],
//				  const struct aes_key *key);
//
// Use the CS3 variant of ciphertext stealing to encrypt or decrypt the last two
// blocks in a message.  The two blocks are located at src and consist of one
// full block of length 16 bytes and one possibly-partial block of length 1 <=
// pn_len <= 16 bytes.  The output is written to dst, which may equal src.
SYM_FUNC_START(aes_cbc_cts_encrypt_aesni)
	_aes_cbc_cts_crypt	1
SYM_FUNC_END(aes_cbc_cts_encrypt_aesni)
SYM_FUNC_START(aes_cbc_cts_decrypt_aesni)
	_aes_cbc_cts_crypt	0
SYM_FUNC_END(aes_cbc_cts_decrypt_aesni)

#ifdef __x86_64__
// void aes_ctr64_crypt_aesni(u8 *dst, const u8 *src, s64 len,
//			      const u64 le_ctr[2],
//			      const struct aes_enckey *key);
SYM_FUNC_START(aes_ctr64_crypt_aesni)
	// Arguments
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	LEN,		ARG2
	.set	LEN32,		ARG2_32	// Used for improved code density
	.set	LE_CTR_PTR,	ARG3	// Used as temp reg after LE_CTR is loaded
	.set	KEY,		ARG4

	// Other local variables
	.set	AESDATA0,	%xmm0
	.set	AESDATA1,	%xmm1
	.set	AESDATA2,	%xmm2
	.set	AESDATA3,	%xmm3
	.set	LE_CTR,		%xmm4	// Current 128-bit little endian counter
	.set	LE_CTR_INC,	%xmm5	// Initialized to (u64[])[1, 0]
	.set	BSWAP_MASK,	%xmm6
	.set	RNDKEY,		%xmm7	// Temporary register for _do_aes
	.set	RNDKEY_PTR,	LE_CTR_PTR  // Temporary register for _do_aes
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes

	// Initialize LE_CTR, BSWAP_MASK, and LE_CTR_INC.
	movdqu		(LE_CTR_PTR), LE_CTR
	movdqa		RODATA(.Lbswap_mask), BSWAP_MASK
	mov		$1, TMP_32
	movd		TMP_32, LE_CTR_INC

	// Encrypt and XOR four blocks (64 bytes) at a time.
	sub		$64, LEN
	jl		.Lctr_loop4_done
.p2align 5
.Lctr_loop4:
.irp i, 0,1,2,3
	movdqa		LE_CTR, AESDATA\i
	pshufb		BSWAP_MASK, AESDATA\i	// => big endian counter
	paddq		LE_CTR_INC, LE_CTR
.endr
	_do_aes		1, 0,1,2,3
	// AESDATA[0-3] now contain four keystream blocks.
.irp i, 0,1,2,3
	movdqu		\i*16(SRC), RNDKEY	// Use RNDKEY as temp register.
	pxor		RNDKEY, AESDATA\i
	movdqu		AESDATA\i, \i*16(DST)
.endr
	add		$64, SRC
	add		$64, DST
	sub		$64, LEN
	jge		.Lctr_loop4
.Lctr_loop4_done:
	add		$64, LEN32
	jz		.Lctr_done

	// 1 <= LEN <= 63 bytes remain.  Prepare four more keystream blocks.
.irp i, 0,1,2,3
	movdqa		LE_CTR, AESDATA\i
	pshufb		BSWAP_MASK, AESDATA\i	// => big endian counter
  .if \i != 3
	paddq		LE_CTR_INC, LE_CTR
  .endif
.endr
	_do_aes		1, 0,1,2,3
	// AESDATA[0-3] now contain four keystream blocks.

	// XOR one block (16 bytes) at a time.
	sub		$16, LEN32
	jl		.Lctr_xor_partial
.Lctr_xor1:
	movdqu		(SRC), RNDKEY		// Use RNDKEY as temp register.
	pxor		RNDKEY, AESDATA0
	movdqu		AESDATA0, (DST)
	movdqa		AESDATA1, AESDATA0
	movdqa		AESDATA2, AESDATA1
	movdqa		AESDATA3, AESDATA2
	add		$16, SRC
	add		$16, DST
	sub		$16, LEN32
	jge		.Lctr_xor1

	// XOR the remaining LEN mod 16 bytes.
.Lctr_xor_partial:
	test		$8, LEN32
	jz		1f
	movq		AESDATA0, TMP
	xor		(SRC), TMP	// XOR 8 bytes.
	mov		TMP, (DST)
	add		$8, SRC
	add		$8, DST
	psrldq		$8, AESDATA0
1:
	test		$4, LEN32
	jz		2f
	movd		AESDATA0, TMP_32
	xor		(SRC), TMP_32	// XOR 4 bytes.
	mov		TMP_32, (DST)
	add		$4, SRC
	add		$4, DST
	psrldq		$4, AESDATA0
2:
	test		$2, LEN32
	jz		3f
	movd		AESDATA0, TMP_32
	xor		(SRC), TMP_16	// XOR 2 bytes.
	mov		TMP_16, (DST)
	add		$2, SRC
	add		$2, DST
	psrldq		$2, AESDATA0
3:
	test		$1, LEN32
	jz		.Lctr_done
	movd		AESDATA0, TMP_32
	xor		(SRC), TMP_8	// XOR 1 byte.
	mov		TMP_8, (DST)
.Lctr_done:
	RET
SYM_FUNC_END(aes_ctr64_crypt_aesni)
#endif // __x86_64__

// Given a 128-bit XTS tweak in the xmm register \tweak, compute the next tweak
// (by multiplying by the polynomial 'x') and write it back to \tweak.
.macro	_next_tweak	tweak, tmp
	pshufd		$0x13, \tweak, \tmp
	paddq		\tweak, \tweak
	psrad		$31, \tmp
	pand		GF_POLY, \tmp
	pxor		\tmp, \tweak
.endm

.macro	_aes_xts_crypt	enc
	// Arguments
	.set	DST,		ARG0
	.set	SRC,		ARG1
	.set	NBLOCKS,	ARG2
	.set	NBLOCKS32,	ARG2_32	// Used for improved code density
	.set	TWEAK_PTR,	ARG3
	.set	KEY,		ARG4

	// Other local variables
#ifdef __x86_64__
	.set	RNDKEY_PTR,	%r9	// Temporary register for _do_aes
#else
	.set	RNDKEY_PTR,	TWEAK_PTR // TWEAK_PTR is clobbered and reloaded later.
#endif
	.set	NROUNDS,	TMP_32	// Temporary register for _do_aes
	.set	AESDATA0,	%xmm0
	.set	AESDATA1,	%xmm1
	.set	AESDATA2,	%xmm2
	.set	AESDATA3,	%xmm3
	.set	GF_POLY,	%xmm4
	.set	RNDKEY,		%xmm5	// Temporary register for _do_aes
	.set	TWEAK,		%xmm6
	.set	SAVED_TWEAK0,	%xmm7
#ifdef __x86_64__
	.set	SAVED_TWEAK1,	%xmm8
	.set	SAVED_TWEAK2,	%xmm9
#endif

	_prologue	uses_arg3=2, uses_arg4=2
#ifdef __i386__
	// Reserve 16-byte aligned space to spill two tweaks.
	push		%ebp
	mov		%esp, %ebp
	and		$~15, %esp
	sub		$32, %esp
#endif

	movdqu		(TWEAK_PTR), TWEAK
	movdqa		RODATA(.Lxts_gf_poly), GF_POLY

	sub		$4, NBLOCKS
	jl		.Lxts_loop4_done\@
.p2align 5
.Lxts_loop4\@:
	// Load the next four source blocks into AESDATA[0-3] and XOR them with
	// their tweaks, advancing the tweak three times in order to do so.
	// Save the four tweaks for later; on 64-bit they all fit into
	// registers, while on 32-bit two tweaks are spilled to the stack.
.irp i, 0,1,2,3
	movdqu		\i*16(SRC), AESDATA\i
	pxor		TWEAK, AESDATA\i
  .if \i != 3
#ifdef __x86_64__
	movdqa		TWEAK, SAVED_TWEAK\i
#else
    .if \i == 0
	movdqa		TWEAK, SAVED_TWEAK0
    .else
	movdqa		TWEAK, (\i-1)*16(%esp)
    .endif
#endif
	_next_tweak	TWEAK, RNDKEY
  .endif
.endr

	// Encrypt or decrypt the blocks.
	_do_aes		\enc, 0,1,2,3

	// XOR the blocks with the saved tweaks.
	pxor		SAVED_TWEAK0, AESDATA0
#ifdef __x86_64__
	pxor		SAVED_TWEAK1, AESDATA1
	pxor		SAVED_TWEAK2, AESDATA2
#else
	pxor		(%esp), AESDATA1
	pxor		16(%esp), AESDATA2
#endif
	pxor		TWEAK, AESDATA3

	// Store the encrypted or decrypted blocks.
.irp i, 0,1,2,3
	movdqu		AESDATA\i, \i*16(DST)
.endr

	_next_tweak	TWEAK, RNDKEY
	add		$64, SRC
	add		$64, DST
	sub		$4, NBLOCKS
	jge		.Lxts_loop4\@
.Lxts_loop4_done\@:
	add		$4, NBLOCKS32
	jz		.Lxts_done\@

.Lxts_loop1\@:
	movdqu		(SRC), AESDATA0
	pxor		TWEAK, AESDATA0
	_do_aes		\enc, 0
	pxor		TWEAK, AESDATA0
	movdqu		AESDATA0, (DST)
	_next_tweak	TWEAK, RNDKEY
	add		$16, SRC
	add		$16, DST
	dec		NBLOCKS32
	jnz		.Lxts_loop1\@

.Lxts_done\@:
#ifdef __i386__
	// Zeroize the stack buffer that tweaks were spilled to.
	pxor		AESDATA0, AESDATA0
	movdqa		AESDATA0, (%esp)
	movdqa		AESDATA0, 16(%esp)
	mov		%ebp, %esp
	pop		%ebp
#endif
	// Store the next tweak.  On 32-bit, reload TWEAK_PTR from stack first.
	_reload_arg3
	movdqu		TWEAK, (TWEAK_PTR)
	_epilogue
.endm

// void aes_xts_encrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      u8 tweak[AES_BLOCK_SIZE],
//			      const struct aes_key *key);
// void aes_xts_decrypt_aesni(u8 *dst, const u8 *src, long nblocks,
//			      u8 tweak[AES_BLOCK_SIZE],
//			      const struct aes_key *key);
//
// `tweak` must have already been encrypted by the tweak key; `key` is just the
// main key.  To allow incremental computation, `tweak` is updated to contain
// the next tweak.
SYM_FUNC_START(aes_xts_encrypt_aesni)
	_aes_xts_crypt	1
SYM_FUNC_END(aes_xts_encrypt_aesni)
SYM_FUNC_START(aes_xts_decrypt_aesni)
	_aes_xts_crypt	0
SYM_FUNC_END(aes_xts_decrypt_aesni)
