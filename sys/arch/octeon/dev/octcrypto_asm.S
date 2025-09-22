/*	$OpenBSD: octcrypto_asm.S,v 1.2 2019/01/03 17:06:22 visa Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/asm.h>

	.set	noreorder
	.set	arch=octeon

/*
 * COP2 registers and operation codes
 */
#define MT_AES_IV		0x0102
#define MT_AES_KEY		0x0104
#define MT_AES_ENC0		0x010a
#define MT_AES_ENC1		0x310b
#define MT_AES_ENC_CBC0		0x0108
#define MT_AES_ENC_CBC1		0x3109
#define MT_AES_DEC_CBC0		0x010c
#define MT_AES_DEC_CBC1		0x310d
#define MF_AES_RESINP		0x0100
#define MT_AES_RESINP		0x0100
#define MT_HSH_DAT		0x0040
#define MT_HSH_DATW		0x0240
#define MT_HSH_IV		0x0048
#define MF_HSH_IV		0x0048
#define MT_HSH_IVW		0x0250
#define MF_HSH_IVW		0x0250
#define MT_HSH_STARTMD5		0x4047
#define MT_HSH_STARTSHA		0x4057
#define MT_HSH_STARTSHA256	0x404f
#define MT_HSH_STARTSHA512	0x424f

#define MT_GFM_MUL		0x0258
#define MT_GFM_POLY		0x025e
#define MF_GFM_RESINP		0x025a
#define MT_GFM_RESINP		0x025a
#define MT_GFM_XOR0		0x025c
#define MT_GFM_XORMUL1		0x425d

#define GF128_POLY		0xe100

/*
 * void octcrypto_aes_set_key(const uint64_t *key, size_t len)
 *
 * Load an AES key to the coprocessor.
 * `len' is in bytes.
 */
LEAF(octcrypto_aes_set_key, 0)
	ld	t0, (a0)
	ld	t1, 8(a0)
	dmtc2	t0, MT_AES_KEY
	bltu	a1, 24, 1f
	 dmtc2	t1, MT_AES_KEY+1	/* 128-bit key */
	ld	t0, 16(a0)
	bltu	a1, 32, 1f
	 dmtc2	t0, MT_AES_KEY+2	/* 192-bit key */
	ld	t0, 24(a0)
	dmtc2	t0, MT_AES_KEY+3	/* 256-bit key */
1:
	jr	ra
	 nop
END(octcrypto_aes_set_key)

/*
 * void octcrypto_aes_clear(void)
 *
 * Clear AES state.
 */
LEAF(octcrypto_aes_clear, 0)
	dmtc2	zero, MT_AES_KEY
	dmtc2	zero, MT_AES_KEY+1
	dmtc2	zero, MT_AES_KEY+2
	dmtc2	zero, MT_AES_KEY+3
	dmtc2	zero, MT_AES_RESINP
	jr	ra
	 dmtc2	zero, MT_AES_RESINP+1
END(octcrypto_aes_clear)

/*
 * void octcrypto_aes_enc(uint64_t *buf)
 *
 * AES encrypt a single block.
 */
LEAF(octcrypto_aes_enc, 0)
	ld	t0, (a0)
	ld	t1, 8(a0)
	dmtc2	t0, MT_AES_ENC0
	dmtc2	t1, MT_AES_ENC1
	dmfc2	t0, MF_AES_RESINP
	dmfc2	t1, MF_AES_RESINP+1
	sd	t0, (a0)
	jr	ra
	 sd	t1, 8(a0)
END(octcrypto_aes_enc)

/*
 * void octcrypto_aes_cbc_enc(void *buf, size_t size, const void *iv)
 *
 * AES CBC encrypt a sequence of blocks.
 */
LEAF(octcrypto_aes_cbc_enc, 0)
	bltu	a1, 16, 3f
	 and	t2, a1, 15
	dsubu	a3, a1, t2
	daddu	a3, a3, a0		/* Compute buffer end. */
	and	t2, a0, 7		/* Determine alignment. */
	ld	t0, (a2)
	ld	t1, 8(a2)
	dmtc2	t0, MT_AES_IV
	bne	t2, zero, 2f
	 dmtc2	t1, MT_AES_IV+1

	/*
	 * Aligned buffer
	 */
1:
	ld	t0, (a0)
	ld	t1, 8(a0)
	daddu	a0, a0, 16
	dmtc2	t0, MT_AES_ENC_CBC0
	dmtc2	t1, MT_AES_ENC_CBC1
	dmfc2	t0, MF_AES_RESINP
	dmfc2	t1, MF_AES_RESINP+1
	sd	t0, -16(a0)
	bltu	a0, a3, 1b
	 sd	t1, -8(a0)
	b	3f
	 nop

	/*
	 * Unaligned buffer
	 */
2:
	LDHI	t0, (a0)
	LDLO	t0, 7(a0)
	LDHI	t1, 8(a0)
	LDLO	t1, 15(a0)
	daddu	a0, a0, 16
	dmtc2	t0, MT_AES_ENC_CBC0
	dmtc2	t1, MT_AES_ENC_CBC1
	dmfc2	t0, MF_AES_RESINP
	dmfc2	t1, MF_AES_RESINP+1
	SDHI	t0, -16(a0)
	SDLO	t0, -9(a0)
	SDHI	t1, -8(a0)
	bltu	a0, a3, 2b
	 SDLO	t1, -1(a0)

3:
	jr	ra
	 nop
END(octcrypto_aes_cbc_enc)

/*
 * void octcrypto_aes_cbc_dec(void *dst, size_t size, const void *iv)
 *
 * AES CBC decrypt a sequence of blocks.
 */
LEAF(octcrypto_aes_cbc_dec, 0)
	bltu	a1, 16, 3f
	 and	t2, a1, 15
	dsubu	a3, a1, t2
	daddu	a3, a3, a0		/* Compute buffer end. */
	and	t2, a0, 7		/* Determine alignment. */
	ld	t0, (a2)
	ld	t1, 8(a2)
	dmtc2	t0, MT_AES_IV
	bne	t2, zero, 2f
	 dmtc2	t1, MT_AES_IV+1

	/*
	 * Aligned buffer
	 */
1:
	ld	t0, (a0)
	ld	t1, 8(a0)
	daddu	a0, a0, 16
	dmtc2	t0, MT_AES_DEC_CBC0
	dmtc2	t1, MT_AES_DEC_CBC1
	dmfc2	t0, MF_AES_RESINP
	dmfc2	t1, MF_AES_RESINP+1
	sd	t0, -16(a0)
	bltu	a0, a3, 1b
	 sd	t1, -8(a0)
	b	3f
	 nop

	/*
	 * Unaligned buffer
	 */
2:
	LDHI	t0, (a0)
	LDLO	t0, 7(a0)
	LDHI	t1, 8(a0)
	LDLO	t1, 15(a0)
	daddu	a0, a0, 16
	dmtc2	t0, MT_AES_DEC_CBC0
	dmtc2	t1, MT_AES_DEC_CBC1
	dmfc2	t0, MF_AES_RESINP
	dmfc2	t1, MF_AES_RESINP+1
	SDHI	t0, -16(a0)
	SDLO	t0, -9(a0)
	SDHI	t1, -8(a0)
	bltu	a0, a3, 2b
	 SDLO	t1, -1(a0)

3:
	jr	ra
	 nop
END(octcrypto_aes_cbc_dec)

/*
 * void octcrypto_aes_ctr_enc(void *buf, size_t size, const void *icb)
 *
 * AES CTR encrypt a sequence of blocks.
 */
LEAF(octcrypto_aes_ctr_enc, 0)
	bltu	a1, 16, 3f
	 and	t2, a1, 15
	dsubu	a3, a1, t2
	daddu	a3, a3, a0		/* Compute buffer end. */
	and	t2, a0, 7		/* Determine alignment. */
	ld	t0, (a2)
	bne	t2, zero, 2f
	 ld	t1, 8(a2)

	/*
	 * Aligned buffer
	 */
1:
	/*
	 * Increment the counter.
	 * Assume big endian byte order and no overflow.
	 */
	daddu	t1, 1
	/* Compute the keystream block. */
	dmtc2	t0, MT_AES_ENC0
	dmtc2	t1, MT_AES_ENC1
	dmfc2	t2, MF_AES_RESINP
	dmfc2	t3, MF_AES_RESINP+1
	/* XOR the plaintext and the keystream. */
	ld	a4, (a0)
	ld	a5, 8(a0)
	daddu	a0, a0, 16
	xor	a4, t2
	xor	a5, t3
	sd	a4, -16(a0)
	bltu	a0, a3, 1b
	 sd	a5, -8(a0)
	b	3f
	 nop

	/*
	 * Unaligned buffer
	 */
2:
	/*
	 * Increment the counter.
	 * Assume big endian byte order and no overflow.
	 */
	daddu	t1, 1
	/* Compute the keystream block. */
	dmtc2	t0, MT_AES_ENC0
	dmtc2	t1, MT_AES_ENC1
	dmfc2	t2, MF_AES_RESINP
	dmfc2	t3, MF_AES_RESINP+1
	/* XOR the plaintext and the keystream. */
	LDHI	a4, (a0)
	LDLO	a4, 7(a0)
	LDHI	a5, 8(a0)
	LDLO	a5, 15(a0)
	daddu	a0, a0, 16
	xor	a4, t2
	xor	a5, t3
	SDHI	a4, -16(a0)
	SDLO	a4, -9(a0)
	SDHI	a5, -8(a0)
	bltu	a0, a3, 2b
	 SDLO	a5, -1(a0)

3:
	jr	ra
	 nop
END(octcrypto_aes_ctr_enc)

#define HASH_NARROW(name, type)						\
LEAF(octcrypto_hash_##name, 0)						\
	bltu	a1, 64, 3f;						\
	 and	t3, a1, 63;						\
	and	t2, a0, 7;		/* Determine alignment. */	\
	dsubu	a3, a1, t3;						\
	bne	t2, zero, 2f;						\
	 daddu	a3, a3, a0;		/* Compute buffer end. */	\
									\
	/*								\
	 * Aligned buffer						\
	 */								\
1:									\
	ld	t0, (a0);						\
	ld	t1, 8(a0);						\
	ld	t2, 16(a0);						\
	ld	t3, 24(a0);						\
	dmtc2	t0, MT_HSH_DAT;						\
	dmtc2	t1, MT_HSH_DAT+1;					\
	dmtc2	t2, MT_HSH_DAT+2;					\
	dmtc2	t3, MT_HSH_DAT+3;					\
	ld	t0, 32(a0);						\
	ld	t1, 40(a0);						\
	ld	t2, 48(a0);						\
	ld	t3, 56(a0);						\
	daddu	a0, a0, 64;						\
	dmtc2	t0, MT_HSH_DAT+4;					\
	dmtc2	t1, MT_HSH_DAT+5;					\
	dmtc2	t2, MT_HSH_DAT+6;					\
	bltu	a0, a3, 1b;						\
	 dmtc2	t3, MT_HSH_START##type;					\
	b	3f;							\
	 nop;								\
									\
	/*								\
	 * Unaligned buffer						\
	 */								\
2:									\
	LDHI	t0, (a0);						\
	LDLO	t0, 7(a0);						\
	LDHI	t1, 8(a0);						\
	LDLO	t1, 15(a0);						\
	LDHI	t2, 16(a0);						\
	LDLO	t2, 23(a0);						\
	LDHI	t3, 24(a0);						\
	LDLO	t3, 31(a0);						\
	dmtc2	t0, MT_HSH_DAT;						\
	dmtc2	t1, MT_HSH_DAT+1;					\
	dmtc2	t2, MT_HSH_DAT+2;					\
	dmtc2	t3, MT_HSH_DAT+3;					\
	LDHI	t0, 32(a0);						\
	LDLO	t0, 39(a0);						\
	LDHI	t1, 40(a0);						\
	LDLO	t1, 47(a0);						\
	LDHI	t2, 48(a0);						\
	LDLO	t2, 55(a0);						\
	LDHI	t3, 56(a0);						\
	LDLO	t3, 63(a0);						\
	daddu	a0, a0, 64;						\
	dmtc2	t0, MT_HSH_DAT+4;					\
	dmtc2	t1, MT_HSH_DAT+5;					\
	dmtc2	t2, MT_HSH_DAT+6;					\
	bltu	a0, a3, 2b;						\
	 dmtc2	t3, MT_HSH_START##type;					\
3:									\
	jr	ra;							\
	 nop;								\
END(octcrypto_hash_##name)

#define HASH_WIDE(name, type)						\
LEAF(octcrypto_hash_##name, 0)						\
	bltu	a1, 128, 3f;						\
	 and	t3, a1, 127;						\
	and	t2, a0, 7;		/* Determine alignment. */	\
	dsubu	a3, a1, t3;						\
	bne	t2, zero, 2f;						\
	 daddu	a3, a3, a0;		/* Compute buffer end. */	\
									\
	/*								\
	 * Aligned buffer						\
	 */								\
1:									\
	ld	t0, (a0);						\
	ld	t1, 8(a0);						\
	ld	t2, 16(a0);						\
	ld	t3, 24(a0);						\
	dmtc2	t0, MT_HSH_DATW;					\
	dmtc2	t1, MT_HSH_DATW+1;					\
	dmtc2	t2, MT_HSH_DATW+2;					\
	dmtc2	t3, MT_HSH_DATW+3;					\
	ld	t0, 32(a0);						\
	ld	t1, 40(a0);						\
	ld	t2, 48(a0);						\
	ld	t3, 56(a0);						\
	dmtc2	t0, MT_HSH_DATW+4;					\
	dmtc2	t1, MT_HSH_DATW+5;					\
	dmtc2	t2, MT_HSH_DATW+6;					\
	dmtc2	t3, MT_HSH_DATW+7;					\
	ld	t0, 64(a0);						\
	ld	t1, 72(a0);						\
	ld	t2, 80(a0);						\
	ld	t3, 88(a0);						\
	dmtc2	t0, MT_HSH_DATW+8;					\
	dmtc2	t1, MT_HSH_DATW+9;					\
	dmtc2	t2, MT_HSH_DATW+10;					\
	dmtc2	t3, MT_HSH_DATW+11;					\
	ld	t0, 96(a0);						\
	ld	t1, 104(a0);						\
	ld	t2, 112(a0);						\
	ld	t3, 120(a0);						\
	daddu	a0, a0, 128;						\
	dmtc2	t0, MT_HSH_DATW+12;					\
	dmtc2	t1, MT_HSH_DATW+13;					\
	dmtc2	t2, MT_HSH_DATW+14;					\
	bltu	a0, a3, 1b;						\
	 dmtc2	t3, MT_HSH_START##type;					\
	b	3f;							\
	 nop;								\
									\
	/*								\
	 * Unaligned buffer						\
	 */								\
2:									\
	LDHI	t0, (a0);						\
	LDLO	t0, 7(a0);						\
	LDHI	t1, 8(a0);						\
	LDLO	t1, 15(a0);						\
	LDHI	t2, 16(a0);						\
	LDLO	t2, 23(a0);						\
	LDHI	t3, 24(a0);						\
	LDLO	t3, 31(a0);						\
	dmtc2	t0, MT_HSH_DATW;					\
	dmtc2	t1, MT_HSH_DATW+1;					\
	dmtc2	t2, MT_HSH_DATW+2;					\
	dmtc2	t3, MT_HSH_DATW+3;					\
	LDHI	t0, 32(a0);						\
	LDLO	t0, 39(a0);						\
	LDHI	t1, 40(a0);						\
	LDLO	t1, 47(a0);						\
	LDHI	t2, 48(a0);						\
	LDLO	t2, 55(a0);						\
	LDHI	t3, 56(a0);						\
	LDLO	t3, 63(a0);						\
	dmtc2	t0, MT_HSH_DATW+4;					\
	dmtc2	t1, MT_HSH_DATW+5;					\
	dmtc2	t2, MT_HSH_DATW+6;					\
	dmtc2	t3, MT_HSH_DATW+7;					\
	LDHI	t0, 64(a0);						\
	LDLO	t0, 71(a0);						\
	LDHI	t1, 72(a0);						\
	LDLO	t1, 79(a0);						\
	LDHI	t2, 80(a0);						\
	LDLO	t2, 87(a0);						\
	LDHI	t3, 88(a0);						\
	LDLO	t3, 95(a0);						\
	dmtc2	t0, MT_HSH_DATW+8;					\
	dmtc2	t1, MT_HSH_DATW+9;					\
	dmtc2	t2, MT_HSH_DATW+10;					\
	dmtc2	t3, MT_HSH_DATW+11;					\
	LDHI	t0, 96(a0);						\
	LDLO	t0, 103(a0);						\
	LDHI	t1, 104(a0);						\
	LDLO	t1, 111(a0);						\
	LDHI	t2, 112(a0);						\
	LDLO	t2, 119(a0);						\
	LDHI	t3, 120(a0);						\
	LDLO	t3, 127(a0);						\
	daddu	a0, a0, 128;						\
	dmtc2	t0, MT_HSH_DATW+12;					\
	dmtc2	t1, MT_HSH_DATW+13;					\
	dmtc2	t2, MT_HSH_DATW+14;					\
	bltu	a0, a3, 2b;						\
	 dmtc2	t3, MT_HSH_START##type;					\
3:									\
	jr	ra;							\
	 nop;								\
END(octcrypto_hash_##name)

/*
 * void octcrypto_md5(const void *buf, size_t size)
 */
HASH_NARROW(md5, MD5)

/*
 * void octcrypto_sha1(const void *buf, size_t size)
 */
HASH_NARROW(sha1, SHA)

/*
 * void octcrypto_sha256(const void *src, size_t size)
 */
HASH_NARROW(sha256, SHA256)

/*
 * void octcrypto_sha512(const void *src, size_t size)
 */
HASH_WIDE(sha512, SHA512)

/*
 * void octcrypto_hash_set_ivn(const uint64_t *iv)
 */
LEAF(octcrypto_hash_set_ivn, 0)
	ld	t0, (a0)
	ld	t1, 8(a0)
	ld	t2, 16(a0)
	ld	t3, 24(a0)
	dmtc2	t0, MT_HSH_IV
	dmtc2	t1, MT_HSH_IV+1
	dmtc2	t2, MT_HSH_IV+2
	jr	ra
	 dmtc2	t3, MT_HSH_IV+3
END(octcrypto_hash_set_ivn)

/*
 * void octcrypto_hash_set_ivw(const uint64_t *iv)
 */
LEAF(octcrypto_hash_set_ivw, 0)
	ld	t0, (a0)
	ld	t1, 8(a0)
	ld	t2, 16(a0)
	ld	t3, 24(a0)
	dmtc2	t0, MT_HSH_IVW
	dmtc2	t1, MT_HSH_IVW+1
	dmtc2	t2, MT_HSH_IVW+2
	dmtc2	t3, MT_HSH_IVW+3
	ld	t0, 32(a0)
	ld	t1, 40(a0)
	ld	t2, 48(a0)
	ld	t3, 56(a0)
	dmtc2	t0, MT_HSH_IVW+4
	dmtc2	t1, MT_HSH_IVW+5
	dmtc2	t2, MT_HSH_IVW+6
	jr	ra
	 dmtc2	t3, MT_HSH_IVW+7
END(octcrypto_hash_set_ivw)

/*
 * void octcrypto_hash_get_ivn(uint64_t *iv)
 */
LEAF(octcrypto_hash_get_ivn, 0)
	dmfc2	t0, MF_HSH_IV
	dmfc2	t1, MF_HSH_IV+1
	dmfc2	t2, MF_HSH_IV+2
	dmfc2	t3, MF_HSH_IV+3
	sd	t0, (a0)
	sd	t1, 8(a0)
	sd	t2, 16(a0)
	jr	ra
	 sd	t3, 24(a0)
END(octcrypto_hash_get_ivn)

/*
 * void octcrypto_hash_get_ivw(uint64_t *iv)
 */
LEAF(octcrypto_hash_get_ivw, 0)
	dmfc2	t0, MF_HSH_IVW
	dmfc2	t1, MF_HSH_IVW+1
	dmfc2	t2, MF_HSH_IVW+2
	dmfc2	t3, MF_HSH_IVW+3
	sd	t0, (a0)
	sd	t1, 8(a0)
	sd	t2, 16(a0)
	sd	t3, 24(a0)
	dmfc2	t0, MF_HSH_IVW+4
	dmfc2	t1, MF_HSH_IVW+5
	dmfc2	t2, MF_HSH_IVW+6
	dmfc2	t3, MF_HSH_IVW+7
	sd	t0, 32(a0)
	sd	t1, 40(a0)
	sd	t2, 48(a0)
	jr	ra
	 sd	t3, 56(a0)
END(octcrypto_hash_get_ivw)

/*
 * void octcrypto_hash_clearn(void)
 */
LEAF(octcrypto_hash_clearn, 0)
	dmtc2	zero, MT_HSH_IV
	dmtc2	zero, MT_HSH_IV+1
	dmtc2	zero, MT_HSH_IV+2
	dmtc2	zero, MT_HSH_IV+3
	dmtc2	zero, MT_HSH_DAT
	dmtc2	zero, MT_HSH_DAT+1
	dmtc2	zero, MT_HSH_DAT+2
	dmtc2	zero, MT_HSH_DAT+3
	dmtc2	zero, MT_HSH_DAT+4
	dmtc2	zero, MT_HSH_DAT+5
	jr	ra
	 dmtc2	zero, MT_HSH_DAT+6
END(octcrypto_hash_clearn)

/*
 * void octcrypto_hash_clearw(void)
 */
LEAF(octcrypto_hash_clearw, 0)
	dmtc2	zero, MT_HSH_IVW
	dmtc2	zero, MT_HSH_IVW+1
	dmtc2	zero, MT_HSH_IVW+2
	dmtc2	zero, MT_HSH_IVW+3
	dmtc2	zero, MT_HSH_IVW+4
	dmtc2	zero, MT_HSH_IVW+5
	dmtc2	zero, MT_HSH_IVW+6
	dmtc2	zero, MT_HSH_IVW+7
	dmtc2	zero, MT_HSH_DATW
	dmtc2	zero, MT_HSH_DATW+1
	dmtc2	zero, MT_HSH_DATW+2
	dmtc2	zero, MT_HSH_DATW+3
	dmtc2	zero, MT_HSH_DATW+4
	dmtc2	zero, MT_HSH_DATW+5
	dmtc2	zero, MT_HSH_DATW+6
	dmtc2	zero, MT_HSH_DATW+7
	dmtc2	zero, MT_HSH_DATW+8
	dmtc2	zero, MT_HSH_DATW+9
	dmtc2	zero, MT_HSH_DATW+10
	dmtc2	zero, MT_HSH_DATW+11
	dmtc2	zero, MT_HSH_DATW+12
	dmtc2	zero, MT_HSH_DATW+13
	jr	ra
	 dmtc2	zero, MT_HSH_DATW+14
END(octcrypto_hash_clearw)

/*
 * void octcrypto_ghash_init(const uint64_t *key, const uint64_t *state)
 *
 * Initialize the Galois field multiplier unit with the GF(2^128) polynomial
 * and given key.
 * If state is given, load it to the unit; otherwise, use zero state.
 */
LEAF(octcrypto_ghash_init, 0)
	/* Set the polynomial. */
	li	t0, GF128_POLY
	dmtc2	t0, MT_GFM_POLY
	/* Set the hash key / multiplier. */
	ld	t0, (a0)
	ld	t1, 8(a0)
	dmtc2	t0, MT_GFM_MUL
	dmtc2	t1, MT_GFM_MUL+1
	/* Initialize the state. */
	bne	a1, zero, 1f
	 move	t0, zero
	b	2f
	 move	t1, zero
1:
	ld	t0, (a1)
	ld	t1, 8(a1)
2:
	dmtc2	t0, MT_GFM_RESINP
	jr	ra
	 dmtc2	t1, MT_GFM_RESINP+1
END(octcrypto_ghash_init)

/*
 * void octcrypto_ghash_finish(uint64_t *x)
 *
 * Store the current GHASH state into buffer `x',
 * and clear the GFM unit's state.
 */
LEAF(octcrypto_ghash_finish, 0)
	dmfc2	t0, MF_GFM_RESINP
	dmfc2	t1, MF_GFM_RESINP+1
	sd	t0, (a0)
	sd	t1, 8(a0)
	dmtc2	zero, MT_GFM_RESINP
	dmtc2	zero, MT_GFM_RESINP+1
	dmtc2	zero, MT_GFM_MUL
	jr	ra
	 dmtc2	zero, MT_GFM_MUL+1
END(octcrypto_ghash_finish)

/*
 * void octcrypto_ghash_update(const void *buf, size_t len)
 *
 * Update the GHASH state.
 *
 * For each X_i in X_0 || X_1 || ... || X_{n-1} = buf:
 *     Y := (Y ^ X_i) * H
 */
LEAF(octcrypto_ghash_update, 0)
	bltu	a1, 16, 3f
	 and	t2, a1, 15
	dsubu	a3, a1, t2
	and	t0, a0, 7
	bne	t0, zero, 2f
	 daddu	a3, a3, a0
1:
	/* Aligned buffer */
	ld	t0, (a0)
	ld	t1, 8(a0)
	daddu	a0, 16
	dmtc2	t0, MT_GFM_XOR0
	bltu	a0, a3, 1b
	 dmtc2	t1, MT_GFM_XORMUL1
	b	3f
	 nop
2:
	/* Unaligned buffer */
	LDHI	t0, (a0)
	LDLO	t0, 7(a0)
	LDHI	t1, 8(a0)
	LDLO	t1, 15(a0)
	daddu	a0, 16
	dmtc2	t0, MT_GFM_XOR0
	bltu	a0, a3, 2b
	 dmtc2	t1, MT_GFM_XORMUL1
3:
	jr	ra
	 nop
END(octcrypto_ghash_update)
