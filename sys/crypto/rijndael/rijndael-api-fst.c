/*	$KAME: rijndael-api-fst.c,v 1.10 2001/05/27 09:34:18 itojun Exp $	*/

/*
 * rijndael-api-fst.c   v2.3   April '2000
 *
 * Optimised ANSI C code
 *
 * authors: v1.0: Antoon Bosselaers
 *          v2.0: Vincent Rijmen
 *          v2.1: Vincent Rijmen
 *          v2.2: Vincent Rijmen
 *          v2.3: Paulo Barreto
 *          v2.4: Vincent Rijmen
 *
 * This code is placed in the public domain.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif

#include <crypto/rijndael/rijndael_local.h>
#include <crypto/rijndael/rijndael-api-fst.h>

#ifndef TRUE
#define TRUE 1
#endif

typedef u_int8_t	BYTE;

int rijndael_makeKey(keyInstance *key, BYTE direction, int keyLen,
	const char *keyMaterial) {

	if (key == NULL) {
		return BAD_KEY_INSTANCE;
	}

	if ((direction == DIR_ENCRYPT) || (direction == DIR_DECRYPT)) {
		key->direction = direction;
	} else {
		return BAD_KEY_DIR;
	}

	if ((keyLen == 128) || (keyLen == 192) || (keyLen == 256)) {
		key->keyLen = keyLen;
	} else {
		return BAD_KEY_MAT;
	}

	if (keyMaterial != NULL) {
		memcpy(key->keyMaterial, keyMaterial, keyLen/8);
	}

	/* initialize key schedule: */
	if (direction == DIR_ENCRYPT) {
		key->Nr = rijndaelKeySetupEnc(key->rk, key->keyMaterial, keyLen);
	} else {
		key->Nr = rijndaelKeySetupDec(key->rk, key->keyMaterial, keyLen);
	}
	rijndaelKeySetupEnc(key->ek, key->keyMaterial, keyLen);
	return TRUE;
}

int rijndael_cipherInit(cipherInstance *cipher, BYTE mode, char *IV) {
	if ((mode == MODE_ECB) || (mode == MODE_CBC) || (mode == MODE_CFB1)) {
		cipher->mode = mode;
	} else {
		return BAD_CIPHER_MODE;
	}
	if (IV != NULL) {
		memcpy(cipher->IV, IV, RIJNDAEL_MAX_IV_SIZE);
	} else {
		memset(cipher->IV, 0, RIJNDAEL_MAX_IV_SIZE);
	}
	return TRUE;
}

int rijndael_blockEncrypt(cipherInstance *cipher, keyInstance *key,
		const BYTE *input, int inputLen, BYTE *outBuffer) {
	int i, k, numBlocks;
	u_int8_t block[16], iv[4][4];

	if (cipher == NULL ||
		key == NULL ||
		key->direction == DIR_DECRYPT) {
		return BAD_CIPHER_STATE;
	}
	if (input == NULL || inputLen <= 0) {
		return 0; /* nothing to do */
	}

	numBlocks = inputLen/128;

	switch (cipher->mode) {
	case MODE_ECB:
		for (i = numBlocks; i > 0; i--) {
			rijndaelEncrypt(key->rk, key->Nr, input, outBuffer);
			input += 16;
			outBuffer += 16;
		}
		break;

	case MODE_CBC:
#if 1 /*STRICT_ALIGN*/
		memcpy(block, cipher->IV, 16);
		memcpy(iv, input, 16);
		((u_int32_t*)block)[0] ^= ((u_int32_t*)iv)[0];
		((u_int32_t*)block)[1] ^= ((u_int32_t*)iv)[1];
		((u_int32_t*)block)[2] ^= ((u_int32_t*)iv)[2];
		((u_int32_t*)block)[3] ^= ((u_int32_t*)iv)[3];
#else
		((u_int32_t*)block)[0] = ((u_int32_t*)cipher->IV)[0] ^ ((u_int32_t*)input)[0];
		((u_int32_t*)block)[1] = ((u_int32_t*)cipher->IV)[1] ^ ((u_int32_t*)input)[1];
		((u_int32_t*)block)[2] = ((u_int32_t*)cipher->IV)[2] ^ ((u_int32_t*)input)[2];
		((u_int32_t*)block)[3] = ((u_int32_t*)cipher->IV)[3] ^ ((u_int32_t*)input)[3];
#endif
		rijndaelEncrypt(key->rk, key->Nr, block, outBuffer);
		input += 16;
		for (i = numBlocks - 1; i > 0; i--) {
#if 1 /*STRICT_ALIGN*/
			memcpy(block, outBuffer, 16);
			memcpy(iv, input, 16);
			((u_int32_t*)block)[0] ^= ((u_int32_t*)iv)[0];
			((u_int32_t*)block)[1] ^= ((u_int32_t*)iv)[1];
			((u_int32_t*)block)[2] ^= ((u_int32_t*)iv)[2];
			((u_int32_t*)block)[3] ^= ((u_int32_t*)iv)[3];
#else
			((u_int32_t*)block)[0] = ((u_int32_t*)outBuffer)[0] ^ ((u_int32_t*)input)[0];
			((u_int32_t*)block)[1] = ((u_int32_t*)outBuffer)[1] ^ ((u_int32_t*)input)[1];
			((u_int32_t*)block)[2] = ((u_int32_t*)outBuffer)[2] ^ ((u_int32_t*)input)[2];
			((u_int32_t*)block)[3] = ((u_int32_t*)outBuffer)[3] ^ ((u_int32_t*)input)[3];
#endif
			outBuffer += 16;
			rijndaelEncrypt(key->rk, key->Nr, block, outBuffer);
			input += 16;
		}
		break;

	case MODE_CFB1:
#if 1 /*STRICT_ALIGN*/
		memcpy(iv, cipher->IV, 16);
#else  /* !STRICT_ALIGN */
		*((u_int32_t*)iv[0]) = *((u_int32_t*)(cipher->IV   ));
		*((u_int32_t*)iv[1]) = *((u_int32_t*)(cipher->IV+ 4));
		*((u_int32_t*)iv[2]) = *((u_int32_t*)(cipher->IV+ 8));
		*((u_int32_t*)iv[3]) = *((u_int32_t*)(cipher->IV+12));
#endif /* ?STRICT_ALIGN */
		for (i = numBlocks; i > 0; i--) {
			for (k = 0; k < 128; k++) {
				*((u_int32_t*) block    ) = *((u_int32_t*)iv[0]);
				*((u_int32_t*)(block+ 4)) = *((u_int32_t*)iv[1]);
				*((u_int32_t*)(block+ 8)) = *((u_int32_t*)iv[2]);
				*((u_int32_t*)(block+12)) = *((u_int32_t*)iv[3]);
				rijndaelEncrypt(key->ek, key->Nr, block,
				    block);
				outBuffer[k/8] ^= (block[0] & 0x80) >> (k & 7);
				iv[0][0] = (iv[0][0] << 1) | (iv[0][1] >> 7);
				iv[0][1] = (iv[0][1] << 1) | (iv[0][2] >> 7);
				iv[0][2] = (iv[0][2] << 1) | (iv[0][3] >> 7);
				iv[0][3] = (iv[0][3] << 1) | (iv[1][0] >> 7);
				iv[1][0] = (iv[1][0] << 1) | (iv[1][1] >> 7);
				iv[1][1] = (iv[1][1] << 1) | (iv[1][2] >> 7);
				iv[1][2] = (iv[1][2] << 1) | (iv[1][3] >> 7);
				iv[1][3] = (iv[1][3] << 1) | (iv[2][0] >> 7);
				iv[2][0] = (iv[2][0] << 1) | (iv[2][1] >> 7);
				iv[2][1] = (iv[2][1] << 1) | (iv[2][2] >> 7);
				iv[2][2] = (iv[2][2] << 1) | (iv[2][3] >> 7);
				iv[2][3] = (iv[2][3] << 1) | (iv[3][0] >> 7);
				iv[3][0] = (iv[3][0] << 1) | (iv[3][1] >> 7);
				iv[3][1] = (iv[3][1] << 1) | (iv[3][2] >> 7);
				iv[3][2] = (iv[3][2] << 1) | (iv[3][3] >> 7);
				iv[3][3] = (iv[3][3] << 1) | ((outBuffer[k/8] >> (7-(k&7))) & 1);
			}
		}
		break;

	default:
		return BAD_CIPHER_STATE;
	}

	explicit_bzero(block, sizeof(block));
	return 128*numBlocks;
}

/**
 * Encrypt data partitioned in octets, using RFC 2040-like padding.
 *
 * @param   input           data to be encrypted (octet sequence)
 * @param   inputOctets		input length in octets (not bits)
 * @param   outBuffer       encrypted output data
 *
 * @return	length in octets (not bits) of the encrypted output buffer.
 */
int rijndael_padEncrypt(cipherInstance *cipher, keyInstance *key,
		const BYTE *input, int inputOctets, BYTE *outBuffer) {
	int i, numBlocks, padLen;
	u_int8_t block[16], *iv, *cp;

	if (cipher == NULL ||
		key == NULL ||
		key->direction == DIR_DECRYPT) {
		return BAD_CIPHER_STATE;
	}
	if (input == NULL || inputOctets <= 0) {
		return 0; /* nothing to do */
	}

	numBlocks = inputOctets/16;

	switch (cipher->mode) {
	case MODE_ECB:
		for (i = numBlocks; i > 0; i--) {
			rijndaelEncrypt(key->rk, key->Nr, input, outBuffer);
			input += 16;
			outBuffer += 16;
		}
		padLen = 16 - (inputOctets - 16*numBlocks);
		if (padLen <= 0 || padLen > 16)
			return BAD_CIPHER_STATE;
		memcpy(block, input, 16 - padLen);
		for (cp = block + 16 - padLen; cp < block + 16; cp++)
			*cp = padLen;
		rijndaelEncrypt(key->rk, key->Nr, block, outBuffer);
		break;

	case MODE_CBC:
		iv = cipher->IV;
		for (i = numBlocks; i > 0; i--) {
			((u_int32_t*)block)[0] = ((const u_int32_t*)input)[0] ^ ((u_int32_t*)iv)[0];
			((u_int32_t*)block)[1] = ((const u_int32_t*)input)[1] ^ ((u_int32_t*)iv)[1];
			((u_int32_t*)block)[2] = ((const u_int32_t*)input)[2] ^ ((u_int32_t*)iv)[2];
			((u_int32_t*)block)[3] = ((const u_int32_t*)input)[3] ^ ((u_int32_t*)iv)[3];
			rijndaelEncrypt(key->rk, key->Nr, block, outBuffer);
			iv = outBuffer;
			input += 16;
			outBuffer += 16;
		}
		padLen = 16 - (inputOctets - 16*numBlocks);
		if (padLen <= 0 || padLen > 16)
			return BAD_CIPHER_STATE;
		for (i = 0; i < 16 - padLen; i++) {
			block[i] = input[i] ^ iv[i];
		}
		for (i = 16 - padLen; i < 16; i++) {
			block[i] = (BYTE)padLen ^ iv[i];
		}
		rijndaelEncrypt(key->rk, key->Nr, block, outBuffer);
		break;

	default:
		return BAD_CIPHER_STATE;
	}

	explicit_bzero(block, sizeof(block));
	return 16*(numBlocks + 1);
}

int rijndael_blockDecrypt(cipherInstance *cipher, keyInstance *key,
		const BYTE *input, int inputLen, BYTE *outBuffer) {
	int i, k, numBlocks;
	u_int8_t block[16], iv[4][4];

	if (cipher == NULL ||
		key == NULL ||
		(cipher->mode != MODE_CFB1 && key->direction == DIR_ENCRYPT)) {
		return BAD_CIPHER_STATE;
	}
	if (input == NULL || inputLen <= 0) {
		return 0; /* nothing to do */
	}

	numBlocks = inputLen/128;

	switch (cipher->mode) {
	case MODE_ECB:
		for (i = numBlocks; i > 0; i--) {
			rijndaelDecrypt(key->rk, key->Nr, input, outBuffer);
			input += 16;
			outBuffer += 16;
		}
		break;

	case MODE_CBC:
#if 1 /*STRICT_ALIGN */
		memcpy(iv, cipher->IV, 16);
#else
		*((u_int32_t*)iv[0]) = *((u_int32_t*)(cipher->IV   ));
		*((u_int32_t*)iv[1]) = *((u_int32_t*)(cipher->IV+ 4));
		*((u_int32_t*)iv[2]) = *((u_int32_t*)(cipher->IV+ 8));
		*((u_int32_t*)iv[3]) = *((u_int32_t*)(cipher->IV+12));
#endif
		for (i = numBlocks; i > 0; i--) {
			rijndaelDecrypt(key->rk, key->Nr, input, block);
			((u_int32_t*)block)[0] ^= *((u_int32_t*)iv[0]);
			((u_int32_t*)block)[1] ^= *((u_int32_t*)iv[1]);
			((u_int32_t*)block)[2] ^= *((u_int32_t*)iv[2]);
			((u_int32_t*)block)[3] ^= *((u_int32_t*)iv[3]);
#if 1 /*STRICT_ALIGN*/
			memcpy(iv, input, 16);
			memcpy(outBuffer, block, 16);
#else
			*((u_int32_t*)iv[0]) = ((u_int32_t*)input)[0]; ((u_int32_t*)outBuffer)[0] = ((u_int32_t*)block)[0];
			*((u_int32_t*)iv[1]) = ((u_int32_t*)input)[1]; ((u_int32_t*)outBuffer)[1] = ((u_int32_t*)block)[1];
			*((u_int32_t*)iv[2]) = ((u_int32_t*)input)[2]; ((u_int32_t*)outBuffer)[2] = ((u_int32_t*)block)[2];
			*((u_int32_t*)iv[3]) = ((u_int32_t*)input)[3]; ((u_int32_t*)outBuffer)[3] = ((u_int32_t*)block)[3];
#endif
			input += 16;
			outBuffer += 16;
		}
		break;

	case MODE_CFB1:
#if 1 /*STRICT_ALIGN */
		memcpy(iv, cipher->IV, 16);
#else
		*((u_int32_t*)iv[0]) = *((u_int32_t*)(cipher->IV));
		*((u_int32_t*)iv[1]) = *((u_int32_t*)(cipher->IV+ 4));
		*((u_int32_t*)iv[2]) = *((u_int32_t*)(cipher->IV+ 8));
		*((u_int32_t*)iv[3]) = *((u_int32_t*)(cipher->IV+12));
#endif
		for (i = numBlocks; i > 0; i--) {
			for (k = 0; k < 128; k++) {
				*((u_int32_t*) block    ) = *((u_int32_t*)iv[0]);
				*((u_int32_t*)(block+ 4)) = *((u_int32_t*)iv[1]);
				*((u_int32_t*)(block+ 8)) = *((u_int32_t*)iv[2]);
				*((u_int32_t*)(block+12)) = *((u_int32_t*)iv[3]);
				rijndaelEncrypt(key->ek, key->Nr, block,
				    block);
				iv[0][0] = (iv[0][0] << 1) | (iv[0][1] >> 7);
				iv[0][1] = (iv[0][1] << 1) | (iv[0][2] >> 7);
				iv[0][2] = (iv[0][2] << 1) | (iv[0][3] >> 7);
				iv[0][3] = (iv[0][3] << 1) | (iv[1][0] >> 7);
				iv[1][0] = (iv[1][0] << 1) | (iv[1][1] >> 7);
				iv[1][1] = (iv[1][1] << 1) | (iv[1][2] >> 7);
				iv[1][2] = (iv[1][2] << 1) | (iv[1][3] >> 7);
				iv[1][3] = (iv[1][3] << 1) | (iv[2][0] >> 7);
				iv[2][0] = (iv[2][0] << 1) | (iv[2][1] >> 7);
				iv[2][1] = (iv[2][1] << 1) | (iv[2][2] >> 7);
				iv[2][2] = (iv[2][2] << 1) | (iv[2][3] >> 7);
				iv[2][3] = (iv[2][3] << 1) | (iv[3][0] >> 7);
				iv[3][0] = (iv[3][0] << 1) | (iv[3][1] >> 7);
				iv[3][1] = (iv[3][1] << 1) | (iv[3][2] >> 7);
				iv[3][2] = (iv[3][2] << 1) | (iv[3][3] >> 7);
				iv[3][3] = (iv[3][3] << 1) | ((input[k/8] >> (7-(k&7))) & 1);
				outBuffer[k/8] ^= (block[0] & 0x80) >> (k & 7);
			}
		}
		break;

	default:
		return BAD_CIPHER_STATE;
	}

	explicit_bzero(block, sizeof(block));
	return 128*numBlocks;
}

int rijndael_padDecrypt(cipherInstance *cipher, keyInstance *key,
		const BYTE *input, int inputOctets, BYTE *outBuffer) {
	int i, numBlocks, padLen, rval;
	u_int8_t block[16];
	u_int32_t iv[4];

	if (cipher == NULL ||
		key == NULL ||
		key->direction == DIR_ENCRYPT) {
		return BAD_CIPHER_STATE;
	}
	if (input == NULL || inputOctets <= 0) {
		return 0; /* nothing to do */
	}
	if (inputOctets % 16 != 0) {
		return BAD_DATA;
	}

	numBlocks = inputOctets/16;

	switch (cipher->mode) {
	case MODE_ECB:
		/* all blocks but last */
		for (i = numBlocks - 1; i > 0; i--) {
			rijndaelDecrypt(key->rk, key->Nr, input, outBuffer);
			input += 16;
			outBuffer += 16;
		}
		/* last block */
		rijndaelDecrypt(key->rk, key->Nr, input, block);
		padLen = block[15];
		if (padLen >= 16) {
			rval = BAD_DATA;
			goto out;
		}
		for (i = 16 - padLen; i < 16; i++) {
			if (block[i] != padLen) {
				rval = BAD_DATA;
				goto out;
			}
		}
		memcpy(outBuffer, block, 16 - padLen);
		break;

	case MODE_CBC:
		memcpy(iv, cipher->IV, 16);
		/* all blocks but last */
		for (i = numBlocks - 1; i > 0; i--) {
			rijndaelDecrypt(key->rk, key->Nr, input, block);
			((u_int32_t*)block)[0] ^= iv[0];
			((u_int32_t*)block)[1] ^= iv[1];
			((u_int32_t*)block)[2] ^= iv[2];
			((u_int32_t*)block)[3] ^= iv[3];
			memcpy(iv, input, 16);
			memcpy(outBuffer, block, 16);
			input += 16;
			outBuffer += 16;
		}
		/* last block */
		rijndaelDecrypt(key->rk, key->Nr, input, block);
		((u_int32_t*)block)[0] ^= iv[0];
		((u_int32_t*)block)[1] ^= iv[1];
		((u_int32_t*)block)[2] ^= iv[2];
		((u_int32_t*)block)[3] ^= iv[3];
		padLen = block[15];
		if (padLen <= 0 || padLen > 16) {
			rval = BAD_DATA;
			goto out;
		}
		for (i = 16 - padLen; i < 16; i++) {
			if (block[i] != padLen) {
				rval = BAD_DATA;
				goto out;
			}
		}
		memcpy(outBuffer, block, 16 - padLen);
		break;

	default:
		return BAD_CIPHER_STATE;
	}

	rval = 16*numBlocks - padLen;

out:
	explicit_bzero(block, sizeof(block));
	return rval;
}
