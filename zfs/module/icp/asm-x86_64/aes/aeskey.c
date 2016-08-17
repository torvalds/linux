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
 *	 list of conditions and the following disclaimer;
 *
 *  2. binary distributions include the above copyright notice, this list
 *	 of conditions and the following disclaimer in their documentation;
 *
 *  3. the name of the copyright holder is not used to endorse products
 *	 built using this software without specific written permission.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 * Issue Date: 20/12/2007
 */

#include <aes/aes_impl.h>
#include "aesopt.h"
#include "aestab.h"
#include "aestab2.h"

/*
 *	Initialise the key schedule from the user supplied key. The key
 *	length can be specified in bytes, with legal values of 16, 24
 *	and 32, or in bits, with legal values of 128, 192 and 256. These
 *	values correspond with Nk values of 4, 6 and 8 respectively.
 *
 *	The following macros implement a single cycle in the key
 *	schedule generation process. The number of cycles needed
 *	for each cx->n_col and nk value is:
 *
 *	nk =		4  5  6  7  8
 *	------------------------------
 *	cx->n_col = 4	10  9  8  7  7
 *	cx->n_col = 5	14 11 10  9  9
 *	cx->n_col = 6	19 15 12 11 11
 *	cx->n_col = 7	21 19 16 13 14
 *	cx->n_col = 8	29 23 19 17 14
 */

/*
 * OpenSolaris changes
 * 1. Added header files aes_impl.h and aestab2.h
 * 2. Changed uint_8t and uint_32t to uint8_t and uint32_t
 * 3. Remove code under ifdef USE_VIA_ACE_IF_PRESENT (always undefined)
 * 4. Removed always-defined ifdefs FUNCS_IN_C, ENC_KEYING_IN_C,
 *	AES_128, AES_192, AES_256, AES_VAR defines
 * 5. Changed aes_encrypt_key* aes_decrypt_key* functions to "static void"
 * 6. Changed N_COLS to MAX_AES_NB
 * 7. Replaced functions aes_encrypt_key and aes_decrypt_key with
 *	OpenSolaris-compatible functions rijndael_key_setup_enc_amd64 and
 *	rijndael_key_setup_dec_amd64
 * 8. cstyled code and removed lint warnings
 */

#if defined(REDUCE_CODE_SIZE)
#define	ls_box ls_sub
	uint32_t	ls_sub(const uint32_t t, const uint32_t n);
#define	inv_mcol im_sub
	uint32_t	im_sub(const uint32_t x);
#ifdef ENC_KS_UNROLL
#undef ENC_KS_UNROLL
#endif
#ifdef DEC_KS_UNROLL
#undef DEC_KS_UNROLL
#endif
#endif	/* REDUCE_CODE_SIZE */


#define	ke4(k, i) \
{	k[4 * (i) + 4] = ss[0] ^= ls_box(ss[3], 3) ^ t_use(r, c)[i]; \
	k[4 * (i) + 5] = ss[1] ^= ss[0]; \
	k[4 * (i) + 6] = ss[2] ^= ss[1]; \
	k[4 * (i) + 7] = ss[3] ^= ss[2]; \
}

static void
aes_encrypt_key128(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[4];

	rk[0] = ss[0] = word_in(key, 0);
	rk[1] = ss[1] = word_in(key, 1);
	rk[2] = ss[2] = word_in(key, 2);
	rk[3] = ss[3] = word_in(key, 3);

#ifdef ENC_KS_UNROLL
	ke4(rk, 0);  ke4(rk, 1);
	ke4(rk, 2);  ke4(rk, 3);
	ke4(rk, 4);  ke4(rk, 5);
	ke4(rk, 6);  ke4(rk, 7);
	ke4(rk, 8);
#else
	{
		uint32_t	i;
		for (i = 0; i < 9; ++i)
			ke4(rk, i);
	}
#endif	/* ENC_KS_UNROLL */
	ke4(rk, 9);
}


#define	kef6(k, i) \
{	k[6 * (i) + 6] = ss[0] ^= ls_box(ss[5], 3) ^ t_use(r, c)[i]; \
	k[6 * (i) + 7] = ss[1] ^= ss[0]; \
	k[6 * (i) + 8] = ss[2] ^= ss[1]; \
	k[6 * (i) + 9] = ss[3] ^= ss[2]; \
}

#define	ke6(k, i) \
{	kef6(k, i); \
	k[6 * (i) + 10] = ss[4] ^= ss[3]; \
	k[6 * (i) + 11] = ss[5] ^= ss[4]; \
}

static void
aes_encrypt_key192(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[6];

	rk[0] = ss[0] = word_in(key, 0);
	rk[1] = ss[1] = word_in(key, 1);
	rk[2] = ss[2] = word_in(key, 2);
	rk[3] = ss[3] = word_in(key, 3);
	rk[4] = ss[4] = word_in(key, 4);
	rk[5] = ss[5] = word_in(key, 5);

#ifdef ENC_KS_UNROLL
	ke6(rk, 0);  ke6(rk, 1);
	ke6(rk, 2);  ke6(rk, 3);
	ke6(rk, 4);  ke6(rk, 5);
	ke6(rk, 6);
#else
	{
		uint32_t	i;
		for (i = 0; i < 7; ++i)
			ke6(rk, i);
	}
#endif	/* ENC_KS_UNROLL */
	kef6(rk, 7);
}



#define	kef8(k, i) \
{	k[8 * (i) + 8] = ss[0] ^= ls_box(ss[7], 3) ^ t_use(r, c)[i]; \
	k[8 * (i) + 9] = ss[1] ^= ss[0]; \
	k[8 * (i) + 10] = ss[2] ^= ss[1]; \
	k[8 * (i) + 11] = ss[3] ^= ss[2]; \
}

#define	ke8(k, i) \
{   kef8(k, i); \
	k[8 * (i) + 12] = ss[4] ^= ls_box(ss[3], 0); \
	k[8 * (i) + 13] = ss[5] ^= ss[4]; \
	k[8 * (i) + 14] = ss[6] ^= ss[5]; \
	k[8 * (i) + 15] = ss[7] ^= ss[6]; \
}

static void
aes_encrypt_key256(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[8];

	rk[0] = ss[0] = word_in(key, 0);
	rk[1] = ss[1] = word_in(key, 1);
	rk[2] = ss[2] = word_in(key, 2);
	rk[3] = ss[3] = word_in(key, 3);
	rk[4] = ss[4] = word_in(key, 4);
	rk[5] = ss[5] = word_in(key, 5);
	rk[6] = ss[6] = word_in(key, 6);
	rk[7] = ss[7] = word_in(key, 7);

#ifdef ENC_KS_UNROLL
	ke8(rk, 0); ke8(rk, 1);
	ke8(rk, 2); ke8(rk, 3);
	ke8(rk, 4); ke8(rk, 5);
#else
	{
		uint32_t	i;
		for (i = 0; i < 6; ++i)
			ke8(rk,  i);
	}
#endif	/* ENC_KS_UNROLL */
	kef8(rk, 6);
}


/*
 * Expand the cipher key into the encryption key schedule.
 *
 * Return the number of rounds for the given cipher key size.
 * The size of the key schedule depends on the number of rounds
 * (which can be computed from the size of the key), i.e. 4 * (Nr + 1).
 *
 * Parameters:
 * rk		AES key schedule 32-bit array to be initialized
 * cipherKey	User key
 * keyBits	AES key size (128, 192, or 256 bits)
 */
int
rijndael_key_setup_enc_amd64(uint32_t rk[], const uint32_t cipherKey[],
    int keyBits)
{
	switch (keyBits) {
	case 128:
		aes_encrypt_key128((unsigned char *)&cipherKey[0], rk);
		return (10);
	case 192:
		aes_encrypt_key192((unsigned char *)&cipherKey[0], rk);
		return (12);
	case 256:
		aes_encrypt_key256((unsigned char *)&cipherKey[0], rk);
		return (14);
	default: /* should never get here */
		break;
	}

	return (0);
}


/* this is used to store the decryption round keys  */
/* in forward or reverse order */

#ifdef AES_REV_DKS
#define	v(n, i)  ((n) - (i) + 2 * ((i) & 3))
#else
#define	v(n, i)  (i)
#endif

#if DEC_ROUND == NO_TABLES
#define	ff(x)   (x)
#else
#define	ff(x)   inv_mcol(x)
#if defined(dec_imvars)
#define	d_vars  dec_imvars
#endif
#endif	/* FUNCS_IN_C & DEC_KEYING_IN_C */


#define	k4e(k, i) \
{	k[v(40, (4 * (i)) + 4)] = ss[0] ^= ls_box(ss[3], 3) ^ t_use(r, c)[i]; \
	k[v(40, (4 * (i)) + 5)] = ss[1] ^= ss[0]; \
	k[v(40, (4 * (i)) + 6)] = ss[2] ^= ss[1]; \
	k[v(40, (4 * (i)) + 7)] = ss[3] ^= ss[2]; \
}

#if 1

#define	kdf4(k, i) \
{	ss[0] = ss[0] ^ ss[2] ^ ss[1] ^ ss[3]; \
	ss[1] = ss[1] ^ ss[3]; \
	ss[2] = ss[2] ^ ss[3]; \
	ss[4] = ls_box(ss[(i + 3) % 4], 3) ^ t_use(r, c)[i]; \
	ss[i % 4] ^= ss[4]; \
	ss[4] ^= k[v(40, (4 * (i)))];   k[v(40, (4 * (i)) + 4)] = ff(ss[4]); \
	ss[4] ^= k[v(40, (4 * (i)) + 1)]; k[v(40, (4 * (i)) + 5)] = ff(ss[4]); \
	ss[4] ^= k[v(40, (4 * (i)) + 2)]; k[v(40, (4 * (i)) + 6)] = ff(ss[4]); \
	ss[4] ^= k[v(40, (4 * (i)) + 3)]; k[v(40, (4 * (i)) + 7)] = ff(ss[4]); \
}

#define	kd4(k, i) \
{	ss[4] = ls_box(ss[(i + 3) % 4], 3) ^ t_use(r, c)[i]; \
	ss[i % 4] ^= ss[4]; ss[4] = ff(ss[4]); \
	k[v(40, (4 * (i)) + 4)] = ss[4] ^= k[v(40, (4 * (i)))]; \
	k[v(40, (4 * (i)) + 5)] = ss[4] ^= k[v(40, (4 * (i)) + 1)]; \
	k[v(40, (4 * (i)) + 6)] = ss[4] ^= k[v(40, (4 * (i)) + 2)]; \
	k[v(40, (4 * (i)) + 7)] = ss[4] ^= k[v(40, (4 * (i)) + 3)]; \
}

#define	kdl4(k, i) \
{	ss[4] = ls_box(ss[(i + 3) % 4], 3) ^ t_use(r, c)[i]; \
	ss[i % 4] ^= ss[4]; \
	k[v(40, (4 * (i)) + 4)] = (ss[0] ^= ss[1]) ^ ss[2] ^ ss[3]; \
	k[v(40, (4 * (i)) + 5)] = ss[1] ^ ss[3]; \
	k[v(40, (4 * (i)) + 6)] = ss[0]; \
	k[v(40, (4 * (i)) + 7)] = ss[1]; \
}

#else

#define	kdf4(k, i) \
{	ss[0] ^= ls_box(ss[3], 3) ^ t_use(r, c)[i]; \
	k[v(40, (4 * (i)) + 4)] = ff(ss[0]); \
	ss[1] ^= ss[0]; k[v(40, (4 * (i)) + 5)] = ff(ss[1]); \
	ss[2] ^= ss[1]; k[v(40, (4 * (i)) + 6)] = ff(ss[2]); \
	ss[3] ^= ss[2]; k[v(40, (4 * (i)) + 7)] = ff(ss[3]); \
}

#define	kd4(k, i) \
{	ss[4] = ls_box(ss[3], 3) ^ t_use(r, c)[i]; \
	ss[0] ^= ss[4]; \
	ss[4] = ff(ss[4]); \
	k[v(40, (4 * (i)) + 4)] = ss[4] ^= k[v(40, (4 * (i)))]; \
	ss[1] ^= ss[0]; \
	k[v(40, (4 * (i)) + 5)] = ss[4] ^= k[v(40, (4 * (i)) + 1)]; \
	ss[2] ^= ss[1]; \
	k[v(40, (4 * (i)) + 6)] = ss[4] ^= k[v(40, (4 * (i)) + 2)]; \
	ss[3] ^= ss[2]; \
	k[v(40, (4 * (i)) + 7)] = ss[4] ^= k[v(40, (4 * (i)) + 3)]; \
}

#define	kdl4(k, i) \
{	ss[0] ^= ls_box(ss[3], 3) ^ t_use(r, c)[i]; \
	k[v(40, (4 * (i)) + 4)] = ss[0]; \
	ss[1] ^= ss[0]; k[v(40, (4 * (i)) + 5)] = ss[1]; \
	ss[2] ^= ss[1]; k[v(40, (4 * (i)) + 6)] = ss[2]; \
	ss[3] ^= ss[2]; k[v(40, (4 * (i)) + 7)] = ss[3]; \
}

#endif

static void
aes_decrypt_key128(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[5];
#if defined(d_vars)
	d_vars;
#endif
	rk[v(40, (0))] = ss[0] = word_in(key, 0);
	rk[v(40, (1))] = ss[1] = word_in(key, 1);
	rk[v(40, (2))] = ss[2] = word_in(key, 2);
	rk[v(40, (3))] = ss[3] = word_in(key, 3);

#ifdef DEC_KS_UNROLL
	kdf4(rk, 0); kd4(rk, 1);
	kd4(rk, 2);  kd4(rk, 3);
	kd4(rk, 4);  kd4(rk, 5);
	kd4(rk, 6);  kd4(rk, 7);
	kd4(rk, 8);  kdl4(rk, 9);
#else
	{
		uint32_t	i;
		for (i = 0; i < 10; ++i)
			k4e(rk, i);
#if !(DEC_ROUND == NO_TABLES)
		for (i = MAX_AES_NB; i < 10 * MAX_AES_NB; ++i)
			rk[i] = inv_mcol(rk[i]);
#endif
	}
#endif	/* DEC_KS_UNROLL */
}



#define	k6ef(k, i) \
{	k[v(48, (6 * (i)) + 6)] = ss[0] ^= ls_box(ss[5], 3) ^ t_use(r, c)[i]; \
	k[v(48, (6 * (i)) + 7)] = ss[1] ^= ss[0]; \
	k[v(48, (6 * (i)) + 8)] = ss[2] ^= ss[1]; \
	k[v(48, (6 * (i)) + 9)] = ss[3] ^= ss[2]; \
}

#define	k6e(k, i) \
{	k6ef(k, i); \
	k[v(48, (6 * (i)) + 10)] = ss[4] ^= ss[3]; \
	k[v(48, (6 * (i)) + 11)] = ss[5] ^= ss[4]; \
}

#define	kdf6(k, i) \
{	ss[0] ^= ls_box(ss[5], 3) ^ t_use(r, c)[i]; \
	k[v(48, (6 * (i)) + 6)] = ff(ss[0]); \
	ss[1] ^= ss[0]; k[v(48, (6 * (i)) + 7)] = ff(ss[1]); \
	ss[2] ^= ss[1]; k[v(48, (6 * (i)) + 8)] = ff(ss[2]); \
	ss[3] ^= ss[2]; k[v(48, (6 * (i)) + 9)] = ff(ss[3]); \
	ss[4] ^= ss[3]; k[v(48, (6 * (i)) + 10)] = ff(ss[4]); \
	ss[5] ^= ss[4]; k[v(48, (6 * (i)) + 11)] = ff(ss[5]); \
}

#define	kd6(k, i) \
{	ss[6] = ls_box(ss[5], 3) ^ t_use(r, c)[i]; \
	ss[0] ^= ss[6]; ss[6] = ff(ss[6]); \
	k[v(48, (6 * (i)) + 6)] = ss[6] ^= k[v(48, (6 * (i)))]; \
	ss[1] ^= ss[0]; \
	k[v(48, (6 * (i)) + 7)] = ss[6] ^= k[v(48, (6 * (i)) + 1)]; \
	ss[2] ^= ss[1]; \
	k[v(48, (6 * (i)) + 8)] = ss[6] ^= k[v(48, (6 * (i)) + 2)]; \
	ss[3] ^= ss[2]; \
	k[v(48, (6 * (i)) + 9)] = ss[6] ^= k[v(48, (6 * (i)) + 3)]; \
	ss[4] ^= ss[3]; \
	k[v(48, (6 * (i)) + 10)] = ss[6] ^= k[v(48, (6 * (i)) + 4)]; \
	ss[5] ^= ss[4]; \
	k[v(48, (6 * (i)) + 11)] = ss[6] ^= k[v(48, (6 * (i)) + 5)]; \
}

#define	kdl6(k, i) \
{	ss[0] ^= ls_box(ss[5], 3) ^ t_use(r, c)[i]; \
	k[v(48, (6 * (i)) + 6)] = ss[0]; \
	ss[1] ^= ss[0]; k[v(48, (6 * (i)) + 7)] = ss[1]; \
	ss[2] ^= ss[1]; k[v(48, (6 * (i)) + 8)] = ss[2]; \
	ss[3] ^= ss[2]; k[v(48, (6 * (i)) + 9)] = ss[3]; \
}

static void
aes_decrypt_key192(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[7];
#if defined(d_vars)
	d_vars;
#endif
	rk[v(48, (0))] = ss[0] = word_in(key, 0);
	rk[v(48, (1))] = ss[1] = word_in(key, 1);
	rk[v(48, (2))] = ss[2] = word_in(key, 2);
	rk[v(48, (3))] = ss[3] = word_in(key, 3);

#ifdef DEC_KS_UNROLL
	ss[4] = word_in(key, 4);
	rk[v(48, (4))] = ff(ss[4]);
	ss[5] = word_in(key, 5);
	rk[v(48, (5))] = ff(ss[5]);
	kdf6(rk, 0); kd6(rk, 1);
	kd6(rk, 2);  kd6(rk, 3);
	kd6(rk, 4);  kd6(rk, 5);
	kd6(rk, 6);  kdl6(rk, 7);
#else
	rk[v(48, (4))] = ss[4] = word_in(key, 4);
	rk[v(48, (5))] = ss[5] = word_in(key, 5);
	{
		uint32_t	i;

		for (i = 0; i < 7; ++i)
			k6e(rk, i);
		k6ef(rk, 7);
#if !(DEC_ROUND == NO_TABLES)
		for (i = MAX_AES_NB; i < 12 * MAX_AES_NB; ++i)
			rk[i] = inv_mcol(rk[i]);
#endif
	}
#endif
}



#define	k8ef(k, i) \
{	k[v(56, (8 * (i)) + 8)] = ss[0] ^= ls_box(ss[7], 3) ^ t_use(r, c)[i]; \
	k[v(56, (8 * (i)) + 9)] = ss[1] ^= ss[0]; \
	k[v(56, (8 * (i)) + 10)] = ss[2] ^= ss[1]; \
	k[v(56, (8 * (i)) + 11)] = ss[3] ^= ss[2]; \
}

#define	k8e(k, i) \
{	k8ef(k, i); \
	k[v(56, (8 * (i)) + 12)] = ss[4] ^= ls_box(ss[3], 0); \
	k[v(56, (8 * (i)) + 13)] = ss[5] ^= ss[4]; \
	k[v(56, (8 * (i)) + 14)] = ss[6] ^= ss[5]; \
	k[v(56, (8 * (i)) + 15)] = ss[7] ^= ss[6]; \
}

#define	kdf8(k, i) \
{	ss[0] ^= ls_box(ss[7], 3) ^ t_use(r, c)[i]; \
	k[v(56, (8 * (i)) + 8)] = ff(ss[0]); \
	ss[1] ^= ss[0]; k[v(56, (8 * (i)) + 9)] = ff(ss[1]); \
	ss[2] ^= ss[1]; k[v(56, (8 * (i)) + 10)] = ff(ss[2]); \
	ss[3] ^= ss[2]; k[v(56, (8 * (i)) + 11)] = ff(ss[3]); \
	ss[4] ^= ls_box(ss[3], 0); k[v(56, (8 * (i)) + 12)] = ff(ss[4]); \
	ss[5] ^= ss[4]; k[v(56, (8 * (i)) + 13)] = ff(ss[5]); \
	ss[6] ^= ss[5]; k[v(56, (8 * (i)) + 14)] = ff(ss[6]); \
	ss[7] ^= ss[6]; k[v(56, (8 * (i)) + 15)] = ff(ss[7]); \
}

#define	kd8(k, i) \
{	ss[8] = ls_box(ss[7], 3) ^ t_use(r, c)[i]; \
	ss[0] ^= ss[8]; \
	ss[8] = ff(ss[8]); \
	k[v(56, (8 * (i)) + 8)] = ss[8] ^= k[v(56, (8 * (i)))]; \
	ss[1] ^= ss[0]; \
	k[v(56, (8 * (i)) + 9)] = ss[8] ^= k[v(56, (8 * (i)) + 1)]; \
	ss[2] ^= ss[1]; \
	k[v(56, (8 * (i)) + 10)] = ss[8] ^= k[v(56, (8 * (i)) + 2)]; \
	ss[3] ^= ss[2]; \
	k[v(56, (8 * (i)) + 11)] = ss[8] ^= k[v(56, (8 * (i)) + 3)]; \
	ss[8] = ls_box(ss[3], 0); \
	ss[4] ^= ss[8]; \
	ss[8] = ff(ss[8]); \
	k[v(56, (8 * (i)) + 12)] = ss[8] ^= k[v(56, (8 * (i)) + 4)]; \
	ss[5] ^= ss[4]; \
	k[v(56, (8 * (i)) + 13)] = ss[8] ^= k[v(56, (8 * (i)) + 5)]; \
	ss[6] ^= ss[5]; \
	k[v(56, (8 * (i)) + 14)] = ss[8] ^= k[v(56, (8 * (i)) + 6)]; \
	ss[7] ^= ss[6]; \
	k[v(56, (8 * (i)) + 15)] = ss[8] ^= k[v(56, (8 * (i)) + 7)]; \
}

#define	kdl8(k, i) \
{	ss[0] ^= ls_box(ss[7], 3) ^ t_use(r, c)[i]; \
	k[v(56, (8 * (i)) + 8)] = ss[0]; \
	ss[1] ^= ss[0]; k[v(56, (8 * (i)) + 9)] = ss[1]; \
	ss[2] ^= ss[1]; k[v(56, (8 * (i)) + 10)] = ss[2]; \
	ss[3] ^= ss[2]; k[v(56, (8 * (i)) + 11)] = ss[3]; \
}

static void
aes_decrypt_key256(const unsigned char *key, uint32_t rk[])
{
	uint32_t	ss[9];
#if defined(d_vars)
	d_vars;
#endif
	rk[v(56, (0))] = ss[0] = word_in(key, 0);
	rk[v(56, (1))] = ss[1] = word_in(key, 1);
	rk[v(56, (2))] = ss[2] = word_in(key, 2);
	rk[v(56, (3))] = ss[3] = word_in(key, 3);

#ifdef DEC_KS_UNROLL
	ss[4] = word_in(key, 4);
	rk[v(56, (4))] = ff(ss[4]);
	ss[5] = word_in(key, 5);
	rk[v(56, (5))] = ff(ss[5]);
	ss[6] = word_in(key, 6);
	rk[v(56, (6))] = ff(ss[6]);
	ss[7] = word_in(key, 7);
	rk[v(56, (7))] = ff(ss[7]);
	kdf8(rk, 0); kd8(rk, 1);
	kd8(rk, 2);  kd8(rk, 3);
	kd8(rk, 4);  kd8(rk, 5);
	kdl8(rk, 6);
#else
	rk[v(56, (4))] = ss[4] = word_in(key, 4);
	rk[v(56, (5))] = ss[5] = word_in(key, 5);
	rk[v(56, (6))] = ss[6] = word_in(key, 6);
	rk[v(56, (7))] = ss[7] = word_in(key, 7);
	{
		uint32_t	i;

		for (i = 0; i < 6; ++i)
			k8e(rk,  i);
		k8ef(rk,  6);
#if !(DEC_ROUND == NO_TABLES)
		for (i = MAX_AES_NB; i < 14 * MAX_AES_NB; ++i)
			rk[i] = inv_mcol(rk[i]);
#endif
	}
#endif	/* DEC_KS_UNROLL */
}


/*
 * Expand the cipher key into the decryption key schedule.
 *
 * Return the number of rounds for the given cipher key size.
 * The size of the key schedule depends on the number of rounds
 * (which can be computed from the size of the key), i.e. 4 * (Nr + 1).
 *
 * Parameters:
 * rk		AES key schedule 32-bit array to be initialized
 * cipherKey	User key
 * keyBits	AES key size (128, 192, or 256 bits)
 */
int
rijndael_key_setup_dec_amd64(uint32_t rk[], const uint32_t cipherKey[],
    int keyBits)
{
	switch (keyBits) {
	case 128:
		aes_decrypt_key128((unsigned char *)&cipherKey[0], rk);
		return (10);
	case 192:
		aes_decrypt_key192((unsigned char *)&cipherKey[0], rk);
		return (12);
	case 256:
		aes_decrypt_key256((unsigned char *)&cipherKey[0], rk);
		return (14);
	default: /* should never get here */
		break;
	}

	return (0);
}
