/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 1998-2007, Brian Gladman, Worcester, UK. All rights reserved.
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software is allowed (with or without
 * changes) provided that:
 *
 *  1. source code distributions include the above copyright notice, this
 *     list of conditions and the following disclaimer;
 *
 *  2. binary distributions include the above copyright notice, this list
 *     of conditions and the following disclaimer in their documentation;
 *
 *  3. the name of the copyright holder is not used to endorse products
 *     built using this software without specific written permission.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 * Issue 20/12/2007
 *
 * I am grateful to Dag Arne Osvik for many discussions of the techniques that
 * can be used to optimise AES assembler code on AMD64/EM64T architectures.
 * Some of the techniques used in this implementation are the result of
 * suggestions made by him for which I am most grateful.
 *
 * An AES implementation for AMD64 processors using the YASM assembler.  This
 * implementation provides only encryption, decryption and hence requires key
 * scheduling support in C. It uses 8k bytes of tables but its encryption and
 * decryption performance is very close to that obtained using large tables.
 * It can use either MS Windows or Gnu/Linux/OpenSolaris OS calling conventions,
 * which are as follows:
 *               ms windows  gnu/linux/opensolaris os
 *
 *   in_blk          rcx     rdi
 *   out_blk         rdx     rsi
 *   context (cx)     r8     rdx
 *
 *   preserved       rsi      -    + rbx, rbp, rsp, r12, r13, r14 & r15
 *   registers       rdi      -      on both
 *
 *   destroyed        -      rsi   + rax, rcx, rdx, r8, r9, r10 & r11
 *   registers        -      rdi     on both
 *
 * The convention used here is that for gnu/linux/opensolaris os.
 *
 * This code provides the standard AES block size (128 bits, 16 bytes) and the
 * three standard AES key sizes (128, 192 and 256 bits). It has the same call
 * interface as my C implementation.  It uses the Microsoft C AMD64 calling
 * conventions in which the three parameters are placed in  rcx, rdx and r8
 * respectively.  The rbx, rsi, rdi, rbp and r12..r15 registers are preserved.
 *
 * OpenSolaris Note:
 * Modified to use GNU/Linux/Solaris calling conventions.
 * That is parameters are placed in rdi, rsi, rdx, and rcx, respectively.
 *
 *     AES_RETURN aes_encrypt(const unsigned char in_blk[],
 *                   unsigned char out_blk[], const aes_encrypt_ctx cx[1])/
 *
 *     AES_RETURN aes_decrypt(const unsigned char in_blk[],
 *                   unsigned char out_blk[], const aes_decrypt_ctx cx[1])/
 *
 *     AES_RETURN aes_encrypt_key<NNN>(const unsigned char key[],
 *                                            const aes_encrypt_ctx cx[1])/
 *
 *     AES_RETURN aes_decrypt_key<NNN>(const unsigned char key[],
 *                                            const aes_decrypt_ctx cx[1])/
 *
 *     AES_RETURN aes_encrypt_key(const unsigned char key[],
 *                           unsigned int len, const aes_decrypt_ctx cx[1])/
 *
 *     AES_RETURN aes_decrypt_key(const unsigned char key[],
 *                           unsigned int len, const aes_decrypt_ctx cx[1])/
 *
 * where <NNN> is 128, 102 or 256.  In the last two calls the length can be in
 * either bits or bytes.
 *
 * Comment in/out the following lines to obtain the desired subroutines. These
 * selections MUST match those in the C header file aesopt.h
 */
#define	AES_REV_DKS	  /* define if key decryption schedule is reversed */

#define	LAST_ROUND_TABLES /* define for the faster version using extra tables */

/*
 * The encryption key schedule has the following in memory layout where N is the
 * number of rounds (10, 12 or 14):
 *
 * lo: | input key (round 0)  |  / each round is four 32-bit words
 *     | encryption round 1   |
 *     | encryption round 2   |
 *     ....
 *     | encryption round N-1 |
 * hi: | encryption round N   |
 *
 * The decryption key schedule is normally set up so that it has the same
 * layout as above by actually reversing the order of the encryption key
 * schedule in memory (this happens when AES_REV_DKS is set):
 *
 * lo: | decryption round 0   | =              | encryption round N   |
 *     | decryption round 1   | = INV_MIX_COL[ | encryption round N-1 | ]
 *     | decryption round 2   | = INV_MIX_COL[ | encryption round N-2 | ]
 *     ....                       ....
 *     | decryption round N-1 | = INV_MIX_COL[ | encryption round 1   | ]
 * hi: | decryption round N   | =              | input key (round 0)  |
 *
 * with rounds except the first and last modified using inv_mix_column()
 * But if AES_REV_DKS is NOT set the order of keys is left as it is for
 * encryption so that it has to be accessed in reverse when used for
 * decryption (although the inverse mix column modifications are done)
 *
 * lo: | decryption round 0   | =              | input key (round 0)  |
 *     | decryption round 1   | = INV_MIX_COL[ | encryption round 1   | ]
 *     | decryption round 2   | = INV_MIX_COL[ | encryption round 2   | ]
 *     ....                       ....
 *     | decryption round N-1 | = INV_MIX_COL[ | encryption round N-1 | ]
 * hi: | decryption round N   | =              | encryption round N   |
 *
 * This layout is faster when the assembler key scheduling provided here
 * is used.
 *
 * End of user defines
 */

/*
 * ---------------------------------------------------------------------------
 * OpenSolaris OS modifications
 *
 * This source originates from Brian Gladman file aes_amd64.asm
 * in http://fp.gladman.plus.com/AES/aes-src-04-03-08.zip
 * with these changes:
 *
 * 1. Removed MS Windows-specific code within DLL_EXPORT, _SEH_, and
 * !__GNUC__ ifdefs.  Also removed ENCRYPTION, DECRYPTION,
 * AES_128, AES_192, AES_256, AES_VAR ifdefs.
 *
 * 2. Translate yasm/nasm %define and .macro definitions to cpp(1) #define
 *
 * 3. Translate yasm/nasm %ifdef/%ifndef to cpp(1) #ifdef
 *
 * 4. Translate Intel/yasm/nasm syntax to ATT/OpenSolaris as(1) syntax
 * (operands reversed, literals prefixed with "$", registers prefixed with "%",
 * and "[register+offset]", addressing changed to "offset(register)",
 * parenthesis in constant expressions "()" changed to square brackets "[]",
 * "." removed from  local (numeric) labels, and other changes.
 * Examples:
 * Intel/yasm/nasm Syntax	ATT/OpenSolaris Syntax
 * mov	rax,(4*20h)		mov	$[4*0x20],%rax
 * mov	rax,[ebx+20h]		mov	0x20(%ebx),%rax
 * lea	rax,[ebx+ecx]		lea	(%ebx,%ecx),%rax
 * sub	rax,[ebx+ecx*4-20h]	sub	-0x20(%ebx,%ecx,4),%rax
 *
 * 5. Added OpenSolaris ENTRY_NP/SET_SIZE macros from
 * /usr/include/sys/asm_linkage.h, lint(1B) guards, and dummy C function
 * definitions for lint.
 *
 * 6. Renamed functions and reordered parameters to match OpenSolaris:
 * Original Gladman interface:
 *	int aes_encrypt(const unsigned char *in,
 *		unsigned char *out, const aes_encrypt_ctx cx[1])/
 *	int aes_decrypt(const unsigned char *in,
 *		unsigned char *out, const aes_encrypt_ctx cx[1])/
 * Note: aes_encrypt_ctx contains ks, a 60 element array of uint32_t,
 * and a union type, inf., containing inf.l, a uint32_t and
 * inf.b, a 4-element array of uint32_t.  Only b[0] in the array (aka "l") is
 * used and contains the key schedule length * 16 where key schedule length is
 * 10, 12, or 14 bytes.
 *
 * OpenSolaris OS interface:
 *	void aes_encrypt_amd64(const aes_ks_t *ks, int Nr,
 *		const uint32_t pt[4], uint32_t ct[4])/
 *	void aes_decrypt_amd64(const aes_ks_t *ks, int Nr,
 *		const uint32_t pt[4], uint32_t ct[4])/
 *	typedef union {uint64_t ks64[(MAX_AES_NR + 1) * 4]/
 *		 uint32_t ks32[(MAX_AES_NR + 1) * 4]/ } aes_ks_t/
 * Note: ks is the AES key schedule, Nr is number of rounds, pt is plain text,
 * ct is crypto text, and MAX_AES_NR is 14.
 * For the x86 64-bit architecture, OpenSolaris OS uses ks32 instead of ks64.
 */

#if defined(lint) || defined(__lint)

#include <sys/types.h>
/* ARGSUSED */
void
aes_encrypt_amd64(const uint32_t rk[], int Nr, const uint32_t pt[4],
	uint32_t ct[4]) {
}
/* ARGSUSED */
void
aes_decrypt_amd64(const uint32_t rk[], int Nr, const uint32_t ct[4],
	uint32_t pt[4]) {
}


#else

#define _ASM
#include <sys/asm_linkage.h>

#define	KS_LENGTH	60

#define	raxd		eax
#define	rdxd		edx
#define	rcxd		ecx
#define	rbxd		ebx
#define	rsid		esi
#define	rdid		edi

#define	raxb		al
#define	rdxb		dl
#define	rcxb		cl
#define	rbxb		bl
#define	rsib		sil
#define	rdib		dil

// finite field multiplies by {02}, {04} and {08}

#define	f2(x) [[x<<1]^[[[x>>7]&1]*0x11b]]
#define	f4(x) [[x<<2]^[[[x>>6]&1]*0x11b]^[[[x>>6]&2]*0x11b]]
#define	f8(x) [[x<<3]^[[[x>>5]&1]*0x11b]^[[[x>>5]&2]*0x11b]^[[[x>>5]&4]*0x11b]]

// finite field multiplies required in table generation

#define	f3(x) [[f2(x)] ^ [x]]
#define	f9(x) [[f8(x)] ^ [x]]
#define	fb(x) [[f8(x)] ^ [f2(x)] ^ [x]]
#define	fd(x) [[f8(x)] ^ [f4(x)] ^ [x]]
#define	fe(x) [[f8(x)] ^ [f4(x)] ^ [f2(x)]]

// macros for expanding S-box data

#define	u8(x) [f2(x)], [x], [x], [f3(x)], [f2(x)], [x], [x], [f3(x)]
#define	v8(x) [fe(x)], [f9(x)], [fd(x)], [fb(x)], [fe(x)], [f9(x)], [fd(x)], [x]
#define	w8(x) [x], 0, 0, 0, [x], 0, 0, 0

#define	enc_vals(x)	\
   .byte x(0x63),x(0x7c),x(0x77),x(0x7b),x(0xf2),x(0x6b),x(0x6f),x(0xc5); \
   .byte x(0x30),x(0x01),x(0x67),x(0x2b),x(0xfe),x(0xd7),x(0xab),x(0x76); \
   .byte x(0xca),x(0x82),x(0xc9),x(0x7d),x(0xfa),x(0x59),x(0x47),x(0xf0); \
   .byte x(0xad),x(0xd4),x(0xa2),x(0xaf),x(0x9c),x(0xa4),x(0x72),x(0xc0); \
   .byte x(0xb7),x(0xfd),x(0x93),x(0x26),x(0x36),x(0x3f),x(0xf7),x(0xcc); \
   .byte x(0x34),x(0xa5),x(0xe5),x(0xf1),x(0x71),x(0xd8),x(0x31),x(0x15); \
   .byte x(0x04),x(0xc7),x(0x23),x(0xc3),x(0x18),x(0x96),x(0x05),x(0x9a); \
   .byte x(0x07),x(0x12),x(0x80),x(0xe2),x(0xeb),x(0x27),x(0xb2),x(0x75); \
   .byte x(0x09),x(0x83),x(0x2c),x(0x1a),x(0x1b),x(0x6e),x(0x5a),x(0xa0); \
   .byte x(0x52),x(0x3b),x(0xd6),x(0xb3),x(0x29),x(0xe3),x(0x2f),x(0x84); \
   .byte x(0x53),x(0xd1),x(0x00),x(0xed),x(0x20),x(0xfc),x(0xb1),x(0x5b); \
   .byte x(0x6a),x(0xcb),x(0xbe),x(0x39),x(0x4a),x(0x4c),x(0x58),x(0xcf); \
   .byte x(0xd0),x(0xef),x(0xaa),x(0xfb),x(0x43),x(0x4d),x(0x33),x(0x85); \
   .byte x(0x45),x(0xf9),x(0x02),x(0x7f),x(0x50),x(0x3c),x(0x9f),x(0xa8); \
   .byte x(0x51),x(0xa3),x(0x40),x(0x8f),x(0x92),x(0x9d),x(0x38),x(0xf5); \
   .byte x(0xbc),x(0xb6),x(0xda),x(0x21),x(0x10),x(0xff),x(0xf3),x(0xd2); \
   .byte x(0xcd),x(0x0c),x(0x13),x(0xec),x(0x5f),x(0x97),x(0x44),x(0x17); \
   .byte x(0xc4),x(0xa7),x(0x7e),x(0x3d),x(0x64),x(0x5d),x(0x19),x(0x73); \
   .byte x(0x60),x(0x81),x(0x4f),x(0xdc),x(0x22),x(0x2a),x(0x90),x(0x88); \
   .byte x(0x46),x(0xee),x(0xb8),x(0x14),x(0xde),x(0x5e),x(0x0b),x(0xdb); \
   .byte x(0xe0),x(0x32),x(0x3a),x(0x0a),x(0x49),x(0x06),x(0x24),x(0x5c); \
   .byte x(0xc2),x(0xd3),x(0xac),x(0x62),x(0x91),x(0x95),x(0xe4),x(0x79); \
   .byte x(0xe7),x(0xc8),x(0x37),x(0x6d),x(0x8d),x(0xd5),x(0x4e),x(0xa9); \
   .byte x(0x6c),x(0x56),x(0xf4),x(0xea),x(0x65),x(0x7a),x(0xae),x(0x08); \
   .byte x(0xba),x(0x78),x(0x25),x(0x2e),x(0x1c),x(0xa6),x(0xb4),x(0xc6); \
   .byte x(0xe8),x(0xdd),x(0x74),x(0x1f),x(0x4b),x(0xbd),x(0x8b),x(0x8a); \
   .byte x(0x70),x(0x3e),x(0xb5),x(0x66),x(0x48),x(0x03),x(0xf6),x(0x0e); \
   .byte x(0x61),x(0x35),x(0x57),x(0xb9),x(0x86),x(0xc1),x(0x1d),x(0x9e); \
   .byte x(0xe1),x(0xf8),x(0x98),x(0x11),x(0x69),x(0xd9),x(0x8e),x(0x94); \
   .byte x(0x9b),x(0x1e),x(0x87),x(0xe9),x(0xce),x(0x55),x(0x28),x(0xdf); \
   .byte x(0x8c),x(0xa1),x(0x89),x(0x0d),x(0xbf),x(0xe6),x(0x42),x(0x68); \
   .byte x(0x41),x(0x99),x(0x2d),x(0x0f),x(0xb0),x(0x54),x(0xbb),x(0x16)

#define	dec_vals(x) \
   .byte x(0x52),x(0x09),x(0x6a),x(0xd5),x(0x30),x(0x36),x(0xa5),x(0x38); \
   .byte x(0xbf),x(0x40),x(0xa3),x(0x9e),x(0x81),x(0xf3),x(0xd7),x(0xfb); \
   .byte x(0x7c),x(0xe3),x(0x39),x(0x82),x(0x9b),x(0x2f),x(0xff),x(0x87); \
   .byte x(0x34),x(0x8e),x(0x43),x(0x44),x(0xc4),x(0xde),x(0xe9),x(0xcb); \
   .byte x(0x54),x(0x7b),x(0x94),x(0x32),x(0xa6),x(0xc2),x(0x23),x(0x3d); \
   .byte x(0xee),x(0x4c),x(0x95),x(0x0b),x(0x42),x(0xfa),x(0xc3),x(0x4e); \
   .byte x(0x08),x(0x2e),x(0xa1),x(0x66),x(0x28),x(0xd9),x(0x24),x(0xb2); \
   .byte x(0x76),x(0x5b),x(0xa2),x(0x49),x(0x6d),x(0x8b),x(0xd1),x(0x25); \
   .byte x(0x72),x(0xf8),x(0xf6),x(0x64),x(0x86),x(0x68),x(0x98),x(0x16); \
   .byte x(0xd4),x(0xa4),x(0x5c),x(0xcc),x(0x5d),x(0x65),x(0xb6),x(0x92); \
   .byte x(0x6c),x(0x70),x(0x48),x(0x50),x(0xfd),x(0xed),x(0xb9),x(0xda); \
   .byte x(0x5e),x(0x15),x(0x46),x(0x57),x(0xa7),x(0x8d),x(0x9d),x(0x84); \
   .byte x(0x90),x(0xd8),x(0xab),x(0x00),x(0x8c),x(0xbc),x(0xd3),x(0x0a); \
   .byte x(0xf7),x(0xe4),x(0x58),x(0x05),x(0xb8),x(0xb3),x(0x45),x(0x06); \
   .byte x(0xd0),x(0x2c),x(0x1e),x(0x8f),x(0xca),x(0x3f),x(0x0f),x(0x02); \
   .byte x(0xc1),x(0xaf),x(0xbd),x(0x03),x(0x01),x(0x13),x(0x8a),x(0x6b); \
   .byte x(0x3a),x(0x91),x(0x11),x(0x41),x(0x4f),x(0x67),x(0xdc),x(0xea); \
   .byte x(0x97),x(0xf2),x(0xcf),x(0xce),x(0xf0),x(0xb4),x(0xe6),x(0x73); \
   .byte x(0x96),x(0xac),x(0x74),x(0x22),x(0xe7),x(0xad),x(0x35),x(0x85); \
   .byte x(0xe2),x(0xf9),x(0x37),x(0xe8),x(0x1c),x(0x75),x(0xdf),x(0x6e); \
   .byte x(0x47),x(0xf1),x(0x1a),x(0x71),x(0x1d),x(0x29),x(0xc5),x(0x89); \
   .byte x(0x6f),x(0xb7),x(0x62),x(0x0e),x(0xaa),x(0x18),x(0xbe),x(0x1b); \
   .byte x(0xfc),x(0x56),x(0x3e),x(0x4b),x(0xc6),x(0xd2),x(0x79),x(0x20); \
   .byte x(0x9a),x(0xdb),x(0xc0),x(0xfe),x(0x78),x(0xcd),x(0x5a),x(0xf4); \
   .byte x(0x1f),x(0xdd),x(0xa8),x(0x33),x(0x88),x(0x07),x(0xc7),x(0x31); \
   .byte x(0xb1),x(0x12),x(0x10),x(0x59),x(0x27),x(0x80),x(0xec),x(0x5f); \
   .byte x(0x60),x(0x51),x(0x7f),x(0xa9),x(0x19),x(0xb5),x(0x4a),x(0x0d); \
   .byte x(0x2d),x(0xe5),x(0x7a),x(0x9f),x(0x93),x(0xc9),x(0x9c),x(0xef); \
   .byte x(0xa0),x(0xe0),x(0x3b),x(0x4d),x(0xae),x(0x2a),x(0xf5),x(0xb0); \
   .byte x(0xc8),x(0xeb),x(0xbb),x(0x3c),x(0x83),x(0x53),x(0x99),x(0x61); \
   .byte x(0x17),x(0x2b),x(0x04),x(0x7e),x(0xba),x(0x77),x(0xd6),x(0x26); \
   .byte x(0xe1),x(0x69),x(0x14),x(0x63),x(0x55),x(0x21),x(0x0c),x(0x7d)

#define	tptr	%rbp	/* table pointer */
#define	kptr	%r8	/* key schedule pointer */
#define	fofs	128	/* adjust offset in key schedule to keep |disp| < 128 */
#define	fk_ref(x, y)	-16*x+fofs+4*y(kptr)

#ifdef	AES_REV_DKS
#define	rofs		128
#define	ik_ref(x, y)	-16*x+rofs+4*y(kptr)

#else
#define	rofs		-128
#define	ik_ref(x, y)	16*x+rofs+4*y(kptr)
#endif	/* AES_REV_DKS */

#define	tab_0(x)	(tptr,x,8)
#define	tab_1(x)	3(tptr,x,8)
#define	tab_2(x)	2(tptr,x,8)
#define	tab_3(x)	1(tptr,x,8)
#define	tab_f(x)	1(tptr,x,8)
#define	tab_i(x)	7(tptr,x,8)

#define	ff_rnd(p1, p2, p3, p4, round)	/* normal forward round */ \
	mov	fk_ref(round,0), p1; \
	mov	fk_ref(round,1), p2; \
	mov	fk_ref(round,2), p3; \
	mov	fk_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	shr	$16, %eax; \
	xor	tab_0(%rsi), p1; \
	xor	tab_1(%rdi), p4; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	xor	tab_2(%rsi), p3; \
	xor	tab_3(%rdi), p2; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	shr	$16, %ebx; \
	xor	tab_0(%rsi), p2; \
	xor	tab_1(%rdi), p1; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	xor	tab_2(%rsi), p4; \
	xor	tab_3(%rdi), p3; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	shr	$16, %ecx; \
	xor	tab_0(%rsi), p3; \
	xor	tab_1(%rdi), p2; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	xor	tab_2(%rsi), p1; \
	xor	tab_3(%rdi), p4; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	shr	$16, %edx; \
	xor	tab_0(%rsi), p4; \
	xor	tab_1(%rdi), p3; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	xor	tab_2(%rsi), p2; \
	xor	tab_3(%rdi), p1; \
 \
	mov	p1, %eax; \
	mov	p2, %ebx; \
	mov	p3, %ecx; \
	mov	p4, %edx

#ifdef	LAST_ROUND_TABLES

#define	fl_rnd(p1, p2, p3, p4, round)	/* last forward round */ \
	add	$2048, tptr; \
	mov	fk_ref(round,0), p1; \
	mov	fk_ref(round,1), p2; \
	mov	fk_ref(round,2), p3; \
	mov	fk_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	shr	$16, %eax; \
	xor	tab_0(%rsi), p1; \
	xor	tab_1(%rdi), p4; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	xor	tab_2(%rsi), p3; \
	xor	tab_3(%rdi), p2; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	shr	$16, %ebx; \
	xor	tab_0(%rsi), p2; \
	xor	tab_1(%rdi), p1; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	xor	tab_2(%rsi), p4; \
	xor	tab_3(%rdi), p3; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	shr	$16, %ecx; \
	xor	tab_0(%rsi), p3; \
	xor	tab_1(%rdi), p2; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	xor	tab_2(%rsi), p1; \
	xor	tab_3(%rdi), p4; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	shr	$16, %edx; \
	xor	tab_0(%rsi), p4; \
	xor	tab_1(%rdi), p3; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	xor	tab_2(%rsi), p2; \
	xor	tab_3(%rdi), p1

#else

#define	fl_rnd(p1, p2, p3, p4, round)	/* last forward round */ \
	mov	fk_ref(round,0), p1; \
	mov	fk_ref(round,1), p2; \
	mov	fk_ref(round,2), p3; \
	mov	fk_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	shr	$16, %eax; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	xor	%esi, p1; \
	rol	$8, %edi; \
	xor	%edi, p4; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p3; \
	xor	%edi, p2; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	shr	$16, %ebx; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	xor	%esi, p2; \
	rol	$8, %edi; \
	xor	%edi, p1; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p4; \
	xor	%edi, p3; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	shr	$16, %ecx; \
	xor	%esi, p3; \
	rol	$8, %edi; \
	xor	%edi, p2; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p1; \
	xor	%edi, p4; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	shr	$16, %edx; \
	xor	%esi, p4; \
	rol	$8, %edi; \
	xor	%edi, p3; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	movzx	tab_f(%rsi), %esi; \
	movzx	tab_f(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p2; \
	xor	%edi, p1

#endif	/* LAST_ROUND_TABLES */

#define	ii_rnd(p1, p2, p3, p4, round)	/* normal inverse round */ \
	mov	ik_ref(round,0), p1; \
	mov	ik_ref(round,1), p2; \
	mov	ik_ref(round,2), p3; \
	mov	ik_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	shr	$16, %eax; \
	xor	tab_0(%rsi), p1; \
	xor	tab_1(%rdi), p2; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	xor	tab_2(%rsi), p3; \
	xor	tab_3(%rdi), p4; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	shr	$16, %ebx; \
	xor	tab_0(%rsi), p2; \
	xor	tab_1(%rdi), p3; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	xor	tab_2(%rsi), p4; \
	xor	tab_3(%rdi), p1; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	shr	$16, %ecx; \
	xor	tab_0(%rsi), p3; \
	xor	tab_1(%rdi), p4; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	xor	tab_2(%rsi), p1; \
	xor	tab_3(%rdi), p2; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	shr	$16, %edx; \
	xor	tab_0(%rsi), p4; \
	xor	tab_1(%rdi), p1; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	xor	tab_2(%rsi), p2; \
	xor	tab_3(%rdi), p3; \
 \
	mov	p1, %eax; \
	mov	p2, %ebx; \
	mov	p3, %ecx; \
	mov	p4, %edx

#ifdef	LAST_ROUND_TABLES

#define	il_rnd(p1, p2, p3, p4, round)	/* last inverse round */ \
	add	$2048, tptr; \
	mov	ik_ref(round,0), p1; \
	mov	ik_ref(round,1), p2; \
	mov	ik_ref(round,2), p3; \
	mov	ik_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	shr	$16, %eax; \
	xor	tab_0(%rsi), p1; \
	xor	tab_1(%rdi), p2; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	xor	tab_2(%rsi), p3; \
	xor	tab_3(%rdi), p4; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	shr	$16, %ebx; \
	xor	tab_0(%rsi), p2; \
	xor	tab_1(%rdi), p3; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	xor	tab_2(%rsi), p4; \
	xor	tab_3(%rdi), p1; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	shr	$16, %ecx; \
	xor	tab_0(%rsi), p3; \
	xor	tab_1(%rdi), p4; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	xor	tab_2(%rsi), p1; \
	xor	tab_3(%rdi), p2; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	shr	$16, %edx; \
	xor	tab_0(%rsi), p4; \
	xor	tab_1(%rdi), p1; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	xor	tab_2(%rsi), p2; \
	xor	tab_3(%rdi), p3

#else

#define	il_rnd(p1, p2, p3, p4, round)	/* last inverse round */ \
	mov	ik_ref(round,0), p1; \
	mov	ik_ref(round,1), p2; \
	mov	ik_ref(round,2), p3; \
	mov	ik_ref(round,3), p4; \
 \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	shr	$16, %eax; \
	xor	%esi, p1; \
	rol	$8, %edi; \
	xor	%edi, p2; \
	movzx	%al, %esi; \
	movzx	%ah, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p3; \
	xor	%edi, p4; \
 \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	shr	$16, %ebx; \
	xor	%esi, p2; \
	rol	$8, %edi; \
	xor	%edi, p3; \
	movzx	%bl, %esi; \
	movzx	%bh, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p4; \
	xor	%edi, p1; \
 \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	shr	$16, %ecx; \
	xor	%esi, p3; \
	rol	$8, %edi; \
	xor	%edi, p4; \
	movzx	%cl, %esi; \
	movzx	%ch, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p1; \
	xor	%edi, p2; \
 \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	shr	$16, %edx; \
	xor	%esi, p4; \
	rol	$8, %edi; \
	xor	%edi, p1; \
	movzx	%dl, %esi; \
	movzx	%dh, %edi; \
	movzx	tab_i(%rsi), %esi; \
	movzx	tab_i(%rdi), %edi; \
	rol	$16, %esi; \
	rol	$24, %edi; \
	xor	%esi, p2; \
	xor	%edi, p3

#endif	/* LAST_ROUND_TABLES */

/*
 * OpenSolaris OS:
 * void aes_encrypt_amd64(const aes_ks_t *ks, int Nr,
 *	const uint32_t pt[4], uint32_t ct[4])/
 *
 * Original interface:
 * int aes_encrypt(const unsigned char *in,
 *	unsigned char *out, const aes_encrypt_ctx cx[1])/
 */
.data
.align	64
enc_tab:
	enc_vals(u8)
#ifdef	LAST_ROUND_TABLES
	// Last Round Tables:
	enc_vals(w8)
#endif


ENTRY_NP(aes_encrypt_amd64)
#ifdef	GLADMAN_INTERFACE
	// Original interface
	sub	$[4*8], %rsp	// gnu/linux/opensolaris binary interface
	mov	%rsi, (%rsp)	// output pointer (P2)
	mov	%rdx, %r8	// context (P3)

	mov	%rbx, 1*8(%rsp)	// P1: input pointer in rdi
	mov	%rbp, 2*8(%rsp)	// P2: output pointer in (rsp)
	mov	%r12, 3*8(%rsp)	// P3: context in r8
	movzx	4*KS_LENGTH(kptr), %esi	// Get byte key length * 16

#else
	// OpenSolaris OS interface
	sub	$[4*8], %rsp	// Make room on stack to save registers
	mov	%rcx, (%rsp)	// Save output pointer (P4) on stack
	mov	%rdi, %r8	// context (P1)
	mov	%rdx, %rdi	// P3: save input pointer
	shl	$4, %esi	// P2: esi byte key length * 16

	mov	%rbx, 1*8(%rsp)	// Save registers
	mov	%rbp, 2*8(%rsp)
	mov	%r12, 3*8(%rsp)
	// P1: context in r8
	// P2: byte key length * 16 in esi
	// P3: input pointer in rdi
	// P4: output pointer in (rsp)
#endif	/* GLADMAN_INTERFACE */

	lea	enc_tab(%rip), tptr
	sub	$fofs, kptr

	// Load input block into registers
	mov	(%rdi), %eax
	mov	1*4(%rdi), %ebx
	mov	2*4(%rdi), %ecx
	mov	3*4(%rdi), %edx

	xor	fofs(kptr), %eax
	xor	fofs+4(kptr), %ebx
	xor	fofs+8(kptr), %ecx
	xor	fofs+12(kptr), %edx

	lea	(kptr,%rsi), kptr
	// Jump based on byte key length * 16:
	cmp	$[10*16], %esi
	je	3f
	cmp	$[12*16], %esi
	je	2f
	cmp	$[14*16], %esi
	je	1f
	mov	$-1, %rax	// error
	jmp	4f

	// Perform normal forward rounds
1:	ff_rnd(%r9d, %r10d, %r11d, %r12d, 13)
	ff_rnd(%r9d, %r10d, %r11d, %r12d, 12)
2:	ff_rnd(%r9d, %r10d, %r11d, %r12d, 11)
	ff_rnd(%r9d, %r10d, %r11d, %r12d, 10)
3:	ff_rnd(%r9d, %r10d, %r11d, %r12d,  9)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  8)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  7)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  6)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  5)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  4)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  3)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  2)
	ff_rnd(%r9d, %r10d, %r11d, %r12d,  1)
	fl_rnd(%r9d, %r10d, %r11d, %r12d,  0)

	// Copy results
	mov	(%rsp), %rbx
	mov	%r9d, (%rbx)
	mov	%r10d, 4(%rbx)
	mov	%r11d, 8(%rbx)
	mov	%r12d, 12(%rbx)
	xor	%rax, %rax
4:	// Restore registers
	mov	1*8(%rsp), %rbx
	mov	2*8(%rsp), %rbp
	mov	3*8(%rsp), %r12
	add	$[4*8], %rsp
	ret

	SET_SIZE(aes_encrypt_amd64)

/*
 * OpenSolaris OS:
 * void aes_decrypt_amd64(const aes_ks_t *ks, int Nr,
 *	const uint32_t pt[4], uint32_t ct[4])/
 *
 * Original interface:
 * int aes_decrypt(const unsigned char *in,
 *	unsigned char *out, const aes_encrypt_ctx cx[1])/
 */
.data
.align	64
dec_tab:
	dec_vals(v8)
#ifdef	LAST_ROUND_TABLES
	// Last Round Tables:
	dec_vals(w8)
#endif


ENTRY_NP(aes_decrypt_amd64)
#ifdef	GLADMAN_INTERFACE
	// Original interface
	sub	$[4*8], %rsp	// gnu/linux/opensolaris binary interface
	mov	%rsi, (%rsp)	// output pointer (P2)
	mov	%rdx, %r8	// context (P3)

	mov	%rbx, 1*8(%rsp)	// P1: input pointer in rdi
	mov	%rbp, 2*8(%rsp)	// P2: output pointer in (rsp)
	mov	%r12, 3*8(%rsp)	// P3: context in r8
	movzx	4*KS_LENGTH(kptr), %esi	// Get byte key length * 16

#else
	// OpenSolaris OS interface
	sub	$[4*8], %rsp	// Make room on stack to save registers
	mov	%rcx, (%rsp)	// Save output pointer (P4) on stack
	mov	%rdi, %r8	// context (P1)
	mov	%rdx, %rdi	// P3: save input pointer
	shl	$4, %esi	// P2: esi byte key length * 16

	mov	%rbx, 1*8(%rsp)	// Save registers
	mov	%rbp, 2*8(%rsp)
	mov	%r12, 3*8(%rsp)
	// P1: context in r8
	// P2: byte key length * 16 in esi
	// P3: input pointer in rdi
	// P4: output pointer in (rsp)
#endif	/* GLADMAN_INTERFACE */

	lea	dec_tab(%rip), tptr
	sub	$rofs, kptr

	// Load input block into registers
	mov	(%rdi), %eax
	mov	1*4(%rdi), %ebx
	mov	2*4(%rdi), %ecx
	mov	3*4(%rdi), %edx

#ifdef AES_REV_DKS
	mov	kptr, %rdi
	lea	(kptr,%rsi), kptr
#else
	lea	(kptr,%rsi), %rdi
#endif

	xor	rofs(%rdi), %eax
	xor	rofs+4(%rdi), %ebx
	xor	rofs+8(%rdi), %ecx
	xor	rofs+12(%rdi), %edx

	// Jump based on byte key length * 16:
	cmp	$[10*16], %esi
	je	3f
	cmp	$[12*16], %esi
	je	2f
	cmp	$[14*16], %esi
	je	1f
	mov	$-1, %rax	// error
	jmp	4f

	// Perform normal inverse rounds
1:	ii_rnd(%r9d, %r10d, %r11d, %r12d, 13)
	ii_rnd(%r9d, %r10d, %r11d, %r12d, 12)
2:	ii_rnd(%r9d, %r10d, %r11d, %r12d, 11)
	ii_rnd(%r9d, %r10d, %r11d, %r12d, 10)
3:	ii_rnd(%r9d, %r10d, %r11d, %r12d,  9)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  8)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  7)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  6)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  5)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  4)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  3)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  2)
	ii_rnd(%r9d, %r10d, %r11d, %r12d,  1)
	il_rnd(%r9d, %r10d, %r11d, %r12d,  0)

	// Copy results
	mov	(%rsp), %rbx
	mov	%r9d, (%rbx)
	mov	%r10d, 4(%rbx)
	mov	%r11d, 8(%rbx)
	mov	%r12d, 12(%rbx)
	xor	%rax, %rax
4:	// Restore registers
	mov	1*8(%rsp), %rbx
	mov	2*8(%rsp), %rbp
	mov	3*8(%rsp), %r12
	add	$[4*8], %rsp
	ret

	SET_SIZE(aes_decrypt_amd64)
#endif	/* lint || __lint */

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
