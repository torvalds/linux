/*
 * ====================================================================
 * Written by Intel Corporation for the OpenSSL project to add support
 * for Intel AES-NI instructions. Rights for redistribution and usage
 * in source and binary forms are granted according to the OpenSSL
 * license.
 *
 *   Author: Huang Ying <ying.huang at intel dot com>
 *           Vinodh Gopal <vinodh.gopal at intel dot com>
 *           Kahraman Akdemir
 *
 * Intel AES-NI is a new set of Single Instruction Multiple Data (SIMD)
 * instructions that are going to be introduced in the next generation
 * of Intel processor, as of 2009. These instructions enable fast and
 * secure data encryption and decryption, using the Advanced Encryption
 * Standard (AES), defined by FIPS Publication number 197. The
 * architecture introduces six instructions that offer full hardware
 * support for AES. Four of them support high performance data
 * encryption and decryption, and the other two instructions support
 * the AES key expansion procedure.
 * ====================================================================
 */

/*
 * ====================================================================
 * Copyright (c) 1998-2008 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 */

/*
 * ====================================================================
 * OpenSolaris OS modifications
 *
 * This source originates as files aes-intel.S and eng_aesni_asm.pl, in
 * patches sent sent Dec. 9, 2008 and Dec. 24, 2008, respectively, by
 * Huang Ying of Intel to the openssl-dev mailing list under the subject
 * of "Add support to Intel AES-NI instruction set for x86_64 platform".
 *
 * This OpenSolaris version has these major changes from the original source:
 *
 * 1. Added OpenSolaris ENTRY_NP/SET_SIZE macros from
 * /usr/include/sys/asm_linkage.h, lint(1B) guards, and dummy C function
 * definitions for lint.
 *
 * 2. Formatted code, added comments, and added #includes and #defines.
 *
 * 3. If bit CR0.TS is set, clear and set the TS bit, after and before
 * calling kpreempt_disable() and kpreempt_enable().
 * If the TS bit is not set, Save and restore %xmm registers at the beginning
 * and end of function calls (%xmm* registers are not saved and restored by
 * during kernel thread preemption).
 *
 * 4. Renamed functions, reordered parameters, and changed return value
 * to match OpenSolaris:
 *
 * OpenSSL interface:
 *	int intel_AES_set_encrypt_key(const unsigned char *userKey,
 *		const int bits, AES_KEY *key);
 *	int intel_AES_set_decrypt_key(const unsigned char *userKey,
 *		const int bits, AES_KEY *key);
 *	Return values for above are non-zero on error, 0 on success.
 *
 *	void intel_AES_encrypt(const unsigned char *in, unsigned char *out,
 *		const AES_KEY *key);
 *	void intel_AES_decrypt(const unsigned char *in, unsigned char *out,
 *		const AES_KEY *key);
 *	typedef struct aes_key_st {
 *		unsigned int	rd_key[4 *(AES_MAXNR + 1)];
 *		int		rounds;
 *		unsigned int	pad[3];
 *	} AES_KEY;
 * Note: AES_LONG is undefined (that is, Intel uses 32-bit key schedules
 * (ks32) instead of 64-bit (ks64).
 * Number of rounds (aka round count) is at offset 240 of AES_KEY.
 *
 * OpenSolaris OS interface (#ifdefs removed for readability):
 *	int rijndael_key_setup_dec_intel(uint32_t rk[],
 *		const uint32_t cipherKey[], uint64_t keyBits);
 *	int rijndael_key_setup_enc_intel(uint32_t rk[],
 *		const uint32_t cipherKey[], uint64_t keyBits);
 *	Return values for above are 0 on error, number of rounds on success.
 *
 *	void aes_encrypt_intel(const aes_ks_t *ks, int Nr,
 *		const uint32_t pt[4], uint32_t ct[4]);
 *	void aes_decrypt_intel(const aes_ks_t *ks, int Nr,
 *		const uint32_t pt[4], uint32_t ct[4]);
 *	typedef union {uint64_t ks64[(MAX_AES_NR + 1) * 4];
 *		 uint32_t ks32[(MAX_AES_NR + 1) * 4]; } aes_ks_t;
 *
 *	typedef union {
 *		uint32_t	ks32[((MAX_AES_NR) + 1) * (MAX_AES_NB)];
 *	} aes_ks_t;
 *	typedef struct aes_key {
 *		aes_ks_t	encr_ks, decr_ks;
 *		long double	align128;
 *		int		flags, nr, type;
 *	} aes_key_t;
 *
 * Note: ks is the AES key schedule, Nr is number of rounds, pt is plain text,
 * ct is crypto text, and MAX_AES_NR is 14.
 * For the x86 64-bit architecture, OpenSolaris OS uses ks32 instead of ks64.
 *
 * Note2: aes_ks_t must be aligned on a 0 mod 128 byte boundary.
 *
 * ====================================================================
 */


#if defined(lint) || defined(__lint)

#include <sys/types.h>

/* ARGSUSED */
void
aes_encrypt_intel(const uint32_t rk[], int Nr, const uint32_t pt[4],
    uint32_t ct[4]) {
}
/* ARGSUSED */
void
aes_decrypt_intel(const uint32_t rk[], int Nr, const uint32_t ct[4],
    uint32_t pt[4]) {
}
/* ARGSUSED */
int
rijndael_key_setup_enc_intel(uint32_t rk[], const uint32_t cipherKey[],
    uint64_t keyBits) {
	return (0);
}
/* ARGSUSED */
int
rijndael_key_setup_dec_intel(uint32_t rk[], const uint32_t cipherKey[],
   uint64_t keyBits) {
	return (0);
}


#else	/* lint */

#define _ASM
#include <sys/asm_linkage.h>


/*
 * _key_expansion_128(), * _key_expansion_192a(), _key_expansion_192b(),
 * _key_expansion_256a(), _key_expansion_256b()
 *
 * Helper functions called by rijndael_key_setup_inc_intel().
 * Also used indirectly by rijndael_key_setup_dec_intel().
 *
 * Input:
 * %xmm0	User-provided cipher key
 * %xmm1	Round constant
 * Output:
 * (%rcx)	AES key
 */

ENTRY_NP2(_key_expansion_128, _key_expansion_256a)
_key_expansion_128_local:
_key_expansion_256a_local:
	pshufd	$0b11111111, %xmm1, %xmm1
	shufps	$0b00010000, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	shufps	$0b10001100, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	pxor	%xmm1, %xmm0
	movups	%xmm0, (%rcx)
	add	$0x10, %rcx
	ret
	nop
SET_SIZE(_key_expansion_128)
SET_SIZE(_key_expansion_256a)


ENTRY_NP(_key_expansion_192a)
_key_expansion_192a_local:
	pshufd	$0b01010101, %xmm1, %xmm1
	shufps	$0b00010000, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	shufps	$0b10001100, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	pxor	%xmm1, %xmm0

	movups	%xmm2, %xmm5
	movups	%xmm2, %xmm6
	pslldq	$4, %xmm5
	pshufd	$0b11111111, %xmm0, %xmm3
	pxor	%xmm3, %xmm2
	pxor	%xmm5, %xmm2

	movups	%xmm0, %xmm1
	shufps	$0b01000100, %xmm0, %xmm6
	movups	%xmm6, (%rcx)
	shufps	$0b01001110, %xmm2, %xmm1
	movups	%xmm1, 0x10(%rcx)
	add	$0x20, %rcx
	ret
SET_SIZE(_key_expansion_192a)


ENTRY_NP(_key_expansion_192b)
_key_expansion_192b_local:
	pshufd	$0b01010101, %xmm1, %xmm1
	shufps	$0b00010000, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	shufps	$0b10001100, %xmm0, %xmm4
	pxor	%xmm4, %xmm0
	pxor	%xmm1, %xmm0

	movups	%xmm2, %xmm5
	pslldq	$4, %xmm5
	pshufd	$0b11111111, %xmm0, %xmm3
	pxor	%xmm3, %xmm2
	pxor	%xmm5, %xmm2

	movups	%xmm0, (%rcx)
	add	$0x10, %rcx
	ret
SET_SIZE(_key_expansion_192b)


ENTRY_NP(_key_expansion_256b)
_key_expansion_256b_local:
	pshufd	$0b10101010, %xmm1, %xmm1
	shufps	$0b00010000, %xmm2, %xmm4
	pxor	%xmm4, %xmm2
	shufps	$0b10001100, %xmm2, %xmm4
	pxor	%xmm4, %xmm2
	pxor	%xmm1, %xmm2
	movups	%xmm2, (%rcx)
	add	$0x10, %rcx
	ret
SET_SIZE(_key_expansion_256b)


/*
 * rijndael_key_setup_enc_intel()
 * Expand the cipher key into the encryption key schedule.
 *
 * For kernel code, caller is responsible for ensuring kpreempt_disable()
 * has been called.  This is because %xmm registers are not saved/restored.
 * Clear and set the CR0.TS bit on entry and exit, respectively,  if TS is set
 * on entry.  Otherwise, if TS is not set, save and restore %xmm registers
 * on the stack.
 *
 * OpenSolaris interface:
 * int rijndael_key_setup_enc_intel(uint32_t rk[], const uint32_t cipherKey[],
 *	uint64_t keyBits);
 * Return value is 0 on error, number of rounds on success.
 *
 * Original Intel OpenSSL interface:
 * int intel_AES_set_encrypt_key(const unsigned char *userKey,
 *	const int bits, AES_KEY *key);
 * Return value is non-zero on error, 0 on success.
 */

#ifdef	OPENSSL_INTERFACE
#define	rijndael_key_setup_enc_intel	intel_AES_set_encrypt_key
#define	rijndael_key_setup_dec_intel	intel_AES_set_decrypt_key

#define	USERCIPHERKEY		rdi	/* P1, 64 bits */
#define	KEYSIZE32		esi	/* P2, 32 bits */
#define	KEYSIZE64		rsi	/* P2, 64 bits */
#define	AESKEY			rdx	/* P3, 64 bits */

#else	/* OpenSolaris Interface */
#define	AESKEY			rdi	/* P1, 64 bits */
#define	USERCIPHERKEY		rsi	/* P2, 64 bits */
#define	KEYSIZE32		edx	/* P3, 32 bits */
#define	KEYSIZE64		rdx	/* P3, 64 bits */
#endif	/* OPENSSL_INTERFACE */

#define	ROUNDS32		KEYSIZE32	/* temp */
#define	ROUNDS64		KEYSIZE64	/* temp */
#define	ENDAESKEY		USERCIPHERKEY	/* temp */

ENTRY_NP(rijndael_key_setup_enc_intel)
rijndael_key_setup_enc_intel_local:
	FRAME_BEGIN
	// NULL pointer sanity check
	test	%USERCIPHERKEY, %USERCIPHERKEY
	jz	.Lenc_key_invalid_param
	test	%AESKEY, %AESKEY
	jz	.Lenc_key_invalid_param

	movups	(%USERCIPHERKEY), %xmm0	// user key (first 16 bytes)
	movups	%xmm0, (%AESKEY)
	lea	0x10(%AESKEY), %rcx	// key addr
	pxor	%xmm4, %xmm4		// xmm4 is assumed 0 in _key_expansion_x

	cmp	$256, %KEYSIZE32
	jnz	.Lenc_key192

	// AES 256: 14 rounds in encryption key schedule
#ifdef OPENSSL_INTERFACE
	mov	$14, %ROUNDS32
	movl	%ROUNDS32, 240(%AESKEY)		// key.rounds = 14
#endif	/* OPENSSL_INTERFACE */

	movups	0x10(%USERCIPHERKEY), %xmm2	// other user key (2nd 16 bytes)
	movups	%xmm2, (%rcx)
	add	$0x10, %rcx

	aeskeygenassist $0x1, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x1, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x2, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x2, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x4, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x4, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x8, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x8, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x10, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x10, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x20, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local
	aeskeygenassist $0x20, %xmm0, %xmm1
	call	_key_expansion_256b_local
	aeskeygenassist $0x40, %xmm2, %xmm1	// expand the key
	call	_key_expansion_256a_local

#ifdef	OPENSSL_INTERFACE
	xor	%rax, %rax			// return 0 (OK)
#else	/* Open Solaris Interface */
	mov	$14, %rax			// return # rounds = 14
#endif
	FRAME_END
	ret

.align 4
.Lenc_key192:
	cmp	$192, %KEYSIZE32
	jnz	.Lenc_key128

	// AES 192: 12 rounds in encryption key schedule
#ifdef OPENSSL_INTERFACE
	mov	$12, %ROUNDS32
	movl	%ROUNDS32, 240(%AESKEY)	// key.rounds = 12
#endif	/* OPENSSL_INTERFACE */

	movq	0x10(%USERCIPHERKEY), %xmm2	// other user key
	aeskeygenassist $0x1, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192a_local
	aeskeygenassist $0x2, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192b_local
	aeskeygenassist $0x4, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192a_local
	aeskeygenassist $0x8, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192b_local
	aeskeygenassist $0x10, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192a_local
	aeskeygenassist $0x20, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192b_local
	aeskeygenassist $0x40, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192a_local
	aeskeygenassist $0x80, %xmm2, %xmm1	// expand the key
	call	_key_expansion_192b_local

#ifdef	OPENSSL_INTERFACE
	xor	%rax, %rax			// return 0 (OK)
#else	/* OpenSolaris Interface */
	mov	$12, %rax			// return # rounds = 12
#endif
	FRAME_END
	ret

.align 4
.Lenc_key128:
	cmp $128, %KEYSIZE32
	jnz .Lenc_key_invalid_key_bits

	// AES 128: 10 rounds in encryption key schedule
#ifdef OPENSSL_INTERFACE
	mov	$10, %ROUNDS32
	movl	%ROUNDS32, 240(%AESKEY)		// key.rounds = 10
#endif	/* OPENSSL_INTERFACE */

	aeskeygenassist $0x1, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x2, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x4, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x8, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x10, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x20, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x40, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x80, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x1b, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local
	aeskeygenassist $0x36, %xmm0, %xmm1	// expand the key
	call	_key_expansion_128_local

#ifdef	OPENSSL_INTERFACE
	xor	%rax, %rax			// return 0 (OK)
#else	/* OpenSolaris Interface */
	mov	$10, %rax			// return # rounds = 10
#endif
	FRAME_END
	ret

.Lenc_key_invalid_param:
#ifdef	OPENSSL_INTERFACE
	mov	$-1, %rax	// user key or AES key pointer is NULL
	FRAME_END
	ret
#else
	/* FALLTHROUGH */
#endif	/* OPENSSL_INTERFACE */

.Lenc_key_invalid_key_bits:
#ifdef	OPENSSL_INTERFACE
	mov	$-2, %rax	// keysize is invalid
#else	/* Open Solaris Interface */
	xor	%rax, %rax	// a key pointer is NULL or invalid keysize
#endif	/* OPENSSL_INTERFACE */
	FRAME_END
	ret
	SET_SIZE(rijndael_key_setup_enc_intel)


/*
 * rijndael_key_setup_dec_intel()
 * Expand the cipher key into the decryption key schedule.
 *
 * For kernel code, caller is responsible for ensuring kpreempt_disable()
 * has been called.  This is because %xmm registers are not saved/restored.
 * Clear and set the CR0.TS bit on entry and exit, respectively,  if TS is set
 * on entry.  Otherwise, if TS is not set, save and restore %xmm registers
 * on the stack.
 *
 * OpenSolaris interface:
 * int rijndael_key_setup_dec_intel(uint32_t rk[], const uint32_t cipherKey[],
 *	uint64_t keyBits);
 * Return value is 0 on error, number of rounds on success.
 * P1->P2, P2->P3, P3->P1
 *
 * Original Intel OpenSSL interface:
 * int intel_AES_set_decrypt_key(const unsigned char *userKey,
 *	const int bits, AES_KEY *key);
 * Return value is non-zero on error, 0 on success.
 */

ENTRY_NP(rijndael_key_setup_dec_intel)
FRAME_BEGIN
	// Generate round keys used for encryption
	call	rijndael_key_setup_enc_intel_local
	test	%rax, %rax
#ifdef	OPENSSL_INTERFACE
	jnz	.Ldec_key_exit	// Failed if returned non-0
#else	/* OpenSolaris Interface */
	jz	.Ldec_key_exit	// Failed if returned 0
#endif	/* OPENSSL_INTERFACE */

	/*
	 * Convert round keys used for encryption
	 * to a form usable for decryption
	 */
#ifndef	OPENSSL_INTERFACE		/* OpenSolaris Interface */
	mov	%rax, %ROUNDS64		// set # rounds (10, 12, or 14)
					// (already set for OpenSSL)
#endif

	lea	0x10(%AESKEY), %rcx	// key addr
	shl	$4, %ROUNDS32
	add	%AESKEY, %ROUNDS64
	mov	%ROUNDS64, %ENDAESKEY

.align 4
.Ldec_key_reorder_loop:
	movups	(%AESKEY), %xmm0
	movups	(%ROUNDS64), %xmm1
	movups	%xmm0, (%ROUNDS64)
	movups	%xmm1, (%AESKEY)
	lea	0x10(%AESKEY), %AESKEY
	lea	-0x10(%ROUNDS64), %ROUNDS64
	cmp	%AESKEY, %ROUNDS64
	ja	.Ldec_key_reorder_loop

.align 4
.Ldec_key_inv_loop:
	movups	(%rcx), %xmm0
	// Convert an encryption round key to a form usable for decryption
	// with the "AES Inverse Mix Columns" instruction
	aesimc	%xmm0, %xmm1
	movups	%xmm1, (%rcx)
	lea	0x10(%rcx), %rcx
	cmp	%ENDAESKEY, %rcx
	jnz	.Ldec_key_inv_loop

.Ldec_key_exit:
	// OpenSolaris: rax = # rounds (10, 12, or 14) or 0 for error
	// OpenSSL: rax = 0 for OK, or non-zero for error
	FRAME_END
	ret
	SET_SIZE(rijndael_key_setup_dec_intel)


/*
 * aes_encrypt_intel()
 * Encrypt a single block (in and out can overlap).
 *
 * For kernel code, caller is responsible for ensuring kpreempt_disable()
 * has been called.  This is because %xmm registers are not saved/restored.
 * Clear and set the CR0.TS bit on entry and exit, respectively,  if TS is set
 * on entry.  Otherwise, if TS is not set, save and restore %xmm registers
 * on the stack.
 *
 * Temporary register usage:
 * %xmm0	State
 * %xmm1	Key
 *
 * Original OpenSolaris Interface:
 * void aes_encrypt_intel(const aes_ks_t *ks, int Nr,
 *	const uint32_t pt[4], uint32_t ct[4])
 *
 * Original Intel OpenSSL Interface:
 * void intel_AES_encrypt(const unsigned char *in, unsigned char *out,
 *	const AES_KEY *key)
 */

#ifdef	OPENSSL_INTERFACE
#define	aes_encrypt_intel	intel_AES_encrypt
#define	aes_decrypt_intel	intel_AES_decrypt

#define	INP		rdi	/* P1, 64 bits */
#define	OUTP		rsi	/* P2, 64 bits */
#define	KEYP		rdx	/* P3, 64 bits */

/* No NROUNDS parameter--offset 240 from KEYP saved in %ecx:  */
#define	NROUNDS32	ecx	/* temporary, 32 bits */
#define	NROUNDS		cl	/* temporary,  8 bits */

#else	/* OpenSolaris Interface */
#define	KEYP		rdi	/* P1, 64 bits */
#define	NROUNDS		esi	/* P2, 32 bits */
#define	INP		rdx	/* P3, 64 bits */
#define	OUTP		rcx	/* P4, 64 bits */
#endif	/* OPENSSL_INTERFACE */

#define	STATE		xmm0	/* temporary, 128 bits */
#define	KEY		xmm1	/* temporary, 128 bits */


ENTRY_NP(aes_encrypt_intel)

	movups	(%INP), %STATE			// input
	movups	(%KEYP), %KEY			// key
#ifdef	OPENSSL_INTERFACE
	mov	240(%KEYP), %NROUNDS32		// round count
#else	/* OpenSolaris Interface */
	/* Round count is already present as P2 in %rsi/%esi */
#endif	/* OPENSSL_INTERFACE */

	pxor	%KEY, %STATE			// round 0
	lea	0x30(%KEYP), %KEYP
	cmp	$12, %NROUNDS
	jb	.Lenc128
	lea	0x20(%KEYP), %KEYP
	je	.Lenc192

	// AES 256
	lea	0x20(%KEYP), %KEYP
	movups	-0x60(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	-0x50(%KEYP), %KEY
	aesenc	%KEY, %STATE

.align 4
.Lenc192:
	// AES 192 and 256
	movups	-0x40(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	-0x30(%KEYP), %KEY
	aesenc	%KEY, %STATE

.align 4
.Lenc128:
	// AES 128, 192, and 256
	movups	-0x20(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	-0x10(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x10(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x20(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x30(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x40(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x50(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x60(%KEYP), %KEY
	aesenc	%KEY, %STATE
	movups	0x70(%KEYP), %KEY
	aesenclast	 %KEY, %STATE		// last round
	movups	%STATE, (%OUTP)			// output

	ret
	SET_SIZE(aes_encrypt_intel)


/*
 * aes_decrypt_intel()
 * Decrypt a single block (in and out can overlap).
 *
 * For kernel code, caller is responsible for ensuring kpreempt_disable()
 * has been called.  This is because %xmm registers are not saved/restored.
 * Clear and set the CR0.TS bit on entry and exit, respectively,  if TS is set
 * on entry.  Otherwise, if TS is not set, save and restore %xmm registers
 * on the stack.
 *
 * Temporary register usage:
 * %xmm0	State
 * %xmm1	Key
 *
 * Original OpenSolaris Interface:
 * void aes_decrypt_intel(const aes_ks_t *ks, int Nr,
 *	const uint32_t pt[4], uint32_t ct[4])/
 *
 * Original Intel OpenSSL Interface:
 * void intel_AES_decrypt(const unsigned char *in, unsigned char *out,
 *	const AES_KEY *key);
 */
ENTRY_NP(aes_decrypt_intel)

	movups	(%INP), %STATE			// input
	movups	(%KEYP), %KEY			// key
#ifdef	OPENSSL_INTERFACE
	mov	240(%KEYP), %NROUNDS32		// round count
#else	/* OpenSolaris Interface */
	/* Round count is already present as P2 in %rsi/%esi */
#endif	/* OPENSSL_INTERFACE */

	pxor	%KEY, %STATE			// round 0
	lea	0x30(%KEYP), %KEYP
	cmp	$12, %NROUNDS
	jb	.Ldec128
	lea	0x20(%KEYP), %KEYP
	je	.Ldec192

	// AES 256
	lea	0x20(%KEYP), %KEYP
	movups	-0x60(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	-0x50(%KEYP), %KEY
	aesdec	%KEY, %STATE

.align 4
.Ldec192:
	// AES 192 and 256
	movups	-0x40(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	-0x30(%KEYP), %KEY
	aesdec	%KEY, %STATE

.align 4
.Ldec128:
	// AES 128, 192, and 256
	movups	-0x20(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	-0x10(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x10(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x20(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x30(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x40(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x50(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x60(%KEYP), %KEY
	aesdec	%KEY, %STATE
	movups	0x70(%KEYP), %KEY
	aesdeclast	%KEY, %STATE		// last round
	movups	%STATE, (%OUTP)			// output

	ret
	SET_SIZE(aes_decrypt_intel)

#endif	/* lint || __lint */

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
