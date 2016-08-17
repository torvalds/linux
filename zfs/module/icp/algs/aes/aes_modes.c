/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <modes/modes.h>
#include <aes/aes_impl.h>

/* Copy a 16-byte AES block from "in" to "out" */
void
aes_copy_block(uint8_t *in, uint8_t *out)
{
	if (IS_P2ALIGNED2(in, out, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[0] = *(uint32_t *)&in[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[4] = *(uint32_t *)&in[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[8] = *(uint32_t *)&in[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[12] = *(uint32_t *)&in[12];
	} else {
		AES_COPY_BLOCK(in, out);
	}
}


/* XOR a 16-byte AES block of data into dst */
void
aes_xor_block(uint8_t *data, uint8_t *dst)
{
	if (IS_P2ALIGNED2(dst, data, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[0] ^= *(uint32_t *)&data[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[4] ^= *(uint32_t *)&data[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[8] ^= *(uint32_t *)&data[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[12] ^= *(uint32_t *)&data[12];
	} else {
		AES_XOR_BLOCK(data, dst);
	}
}


/*
 * Encrypt multiple blocks of data according to mode.
 */
int
aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	if (aes_ctx->ac_flags & CTR_MODE) {
		rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
	} else if (aes_ctx->ac_flags & CCM_MODE) {
		rv = ccm_mode_encrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
	} else if (aes_ctx->ac_flags & (GCM_MODE|GMAC_MODE)) {
		rv = gcm_mode_encrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
	} else if (aes_ctx->ac_flags & CBC_MODE) {
		rv = cbc_encrypt_contiguous_blocks(ctx,
		    data, length, out, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_copy_block, aes_xor_block);
	} else {
		rv = ecb_cipher_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block);
	}
	return (rv);
}


/*
 * Decrypt multiple blocks of data according to mode.
 */
int
aes_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	if (aes_ctx->ac_flags & CTR_MODE) {
		rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
	} else if (aes_ctx->ac_flags & CCM_MODE) {
		rv = ccm_mode_decrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
	} else if (aes_ctx->ac_flags & (GCM_MODE|GMAC_MODE)) {
		rv = gcm_mode_decrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
	} else if (aes_ctx->ac_flags & CBC_MODE) {
		rv = cbc_decrypt_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_decrypt_block, aes_copy_block,
		    aes_xor_block);
	} else {
		rv = ecb_cipher_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_decrypt_block);
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
	}
	return (rv);
}
