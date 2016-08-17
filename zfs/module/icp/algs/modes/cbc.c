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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Algorithm independent CBC functions.
 */
int
cbc_encrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	size_t need = 0;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *lastp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;

	if (length + ctx->cbc_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->cbc_remainder + ctx->cbc_remainder_len,
		    length);
		ctx->cbc_remainder_len += length;
		ctx->cbc_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	lastp = (uint8_t *)ctx->cbc_iv;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		/* Unprocessed data from last call. */
		if (ctx->cbc_remainder_len > 0) {
			need = block_size - ctx->cbc_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->cbc_remainder)
			    [ctx->cbc_remainder_len], need);

			blockp = (uint8_t *)ctx->cbc_remainder;
		} else {
			blockp = datap;
		}

		if (out == NULL) {
			/*
			 * XOR the previous cipher block or IV with the
			 * current clear block.
			 */
			xor_block(lastp, blockp);
			encrypt(ctx->cbc_keysched, blockp, blockp);

			ctx->cbc_lastp = blockp;
			lastp = blockp;

			if (ctx->cbc_remainder_len > 0) {
				bcopy(blockp, ctx->cbc_copy_to,
				    ctx->cbc_remainder_len);
				bcopy(blockp + ctx->cbc_remainder_len, datap,
				    need);
			}
		} else {
			/*
			 * XOR the previous cipher block or IV with the
			 * current clear block.
			 */
			xor_block(blockp, lastp);
			encrypt(ctx->cbc_keysched, lastp, lastp);
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, block_size);

			/* copy block to where it belongs */
			if (out_data_1_len == block_size) {
				copy_block(lastp, out_data_1);
			} else {
				bcopy(lastp, out_data_1, out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(lastp + out_data_1_len,
					    out_data_2,
					    block_size - out_data_1_len);
				}
			}
			/* update offset */
			out->cd_offset += block_size;
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->cbc_remainder_len != 0) {
			datap += need;
			ctx->cbc_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->cbc_remainder, remainder);
			ctx->cbc_remainder_len = remainder;
			ctx->cbc_copy_to = datap;
			goto out;
		}
		ctx->cbc_copy_to = NULL;

	} while (remainder > 0);

out:
	/*
	 * Save the last encrypted block in the context.
	 */
	if (ctx->cbc_lastp != NULL) {
		copy_block((uint8_t *)ctx->cbc_lastp, (uint8_t *)ctx->cbc_iv);
		ctx->cbc_lastp = (uint8_t *)ctx->cbc_iv;
	}

	return (CRYPTO_SUCCESS);
}

#define	OTHER(a, ctx) \
	(((a) == (ctx)->cbc_lastblock) ? (ctx)->cbc_iv : (ctx)->cbc_lastblock)

/* ARGSUSED */
int
cbc_decrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*decrypt)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	size_t need = 0;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *lastp;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;

	if (length + ctx->cbc_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->cbc_remainder + ctx->cbc_remainder_len,
		    length);
		ctx->cbc_remainder_len += length;
		ctx->cbc_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	lastp = ctx->cbc_lastp;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		/* Unprocessed data from last call. */
		if (ctx->cbc_remainder_len > 0) {
			need = block_size - ctx->cbc_remainder_len;

			if (need > remainder)
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->cbc_remainder)
			    [ctx->cbc_remainder_len], need);

			blockp = (uint8_t *)ctx->cbc_remainder;
		} else {
			blockp = datap;
		}

		/* LINTED: pointer alignment */
		copy_block(blockp, (uint8_t *)OTHER((uint64_t *)lastp, ctx));

		if (out != NULL) {
			decrypt(ctx->cbc_keysched, blockp,
			    (uint8_t *)ctx->cbc_remainder);
			blockp = (uint8_t *)ctx->cbc_remainder;
		} else {
			decrypt(ctx->cbc_keysched, blockp, blockp);
		}

		/*
		 * XOR the previous cipher block or IV with the
		 * currently decrypted block.
		 */
		xor_block(lastp, blockp);

		/* LINTED: pointer alignment */
		lastp = (uint8_t *)OTHER((uint64_t *)lastp, ctx);

		if (out != NULL) {
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, block_size);

			bcopy(blockp, out_data_1, out_data_1_len);
			if (out_data_2 != NULL) {
				bcopy(blockp + out_data_1_len, out_data_2,
				    block_size - out_data_1_len);
			}

			/* update offset */
			out->cd_offset += block_size;

		} else if (ctx->cbc_remainder_len > 0) {
			/* copy temporary block to where it belongs */
			bcopy(blockp, ctx->cbc_copy_to, ctx->cbc_remainder_len);
			bcopy(blockp + ctx->cbc_remainder_len, datap, need);
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->cbc_remainder_len != 0) {
			datap += need;
			ctx->cbc_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->cbc_remainder, remainder);
			ctx->cbc_remainder_len = remainder;
			ctx->cbc_lastp = lastp;
			ctx->cbc_copy_to = datap;
			return (CRYPTO_SUCCESS);
		}
		ctx->cbc_copy_to = NULL;

	} while (remainder > 0);

	ctx->cbc_lastp = lastp;
	return (CRYPTO_SUCCESS);
}

int
cbc_init_ctx(cbc_ctx_t *cbc_ctx, char *param, size_t param_len,
    size_t block_size, void (*copy_block)(uint8_t *, uint64_t *))
{
	/*
	 * Copy IV into context.
	 *
	 * If cm_param == NULL then the IV comes from the
	 * cd_miscdata field in the crypto_data structure.
	 */
	if (param != NULL) {
		ASSERT(param_len == block_size);
		copy_block((uchar_t *)param, cbc_ctx->cbc_iv);
	}

	cbc_ctx->cbc_lastp = (uint8_t *)&cbc_ctx->cbc_iv[0];
	cbc_ctx->cbc_flags |= CBC_MODE;
	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
void *
cbc_alloc_ctx(int kmflag)
{
	cbc_ctx_t *cbc_ctx;

	if ((cbc_ctx = kmem_zalloc(sizeof (cbc_ctx_t), kmflag)) == NULL)
		return (NULL);

	cbc_ctx->cbc_flags = CBC_MODE;
	return (cbc_ctx);
}
