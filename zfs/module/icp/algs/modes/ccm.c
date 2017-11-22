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

#if defined(__i386) || defined(__amd64)
#include <sys/byteorder.h>
#define	UNALIGNED_POINTERS_PERMITTED
#endif

/*
 * Encrypt multiple blocks of data in CCM mode.  Decrypt for CCM mode
 * is done in another function.
 */
int
ccm_mode_encrypt_contiguous_blocks(ccm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
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
	uint64_t counter;
	uint8_t *mac_buf;

	if (length + ctx->ccm_remainder_len < block_size) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->ccm_remainder + ctx->ccm_remainder_len,
		    length);
		ctx->ccm_remainder_len += length;
		ctx->ccm_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	lastp = (uint8_t *)ctx->ccm_cb;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	mac_buf = (uint8_t *)ctx->ccm_mac_buf;

	do {
		/* Unprocessed data from last call. */
		if (ctx->ccm_remainder_len > 0) {
			need = block_size - ctx->ccm_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->ccm_remainder)
			    [ctx->ccm_remainder_len], need);

			blockp = (uint8_t *)ctx->ccm_remainder;
		} else {
			blockp = datap;
		}

		/*
		 * do CBC MAC
		 *
		 * XOR the previous cipher block current clear block.
		 * mac_buf always contain previous cipher block.
		 */
		xor_block(blockp, mac_buf);
		encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);

		/* ccm_cb is the counter block */
		encrypt_block(ctx->ccm_keysched, (uint8_t *)ctx->ccm_cb,
		    (uint8_t *)ctx->ccm_tmp);

		lastp = (uint8_t *)ctx->ccm_tmp;

		/*
		 * Increment counter. Counter bits are confined
		 * to the bottom 64 bits of the counter block.
		 */
#ifdef _LITTLE_ENDIAN
		counter = ntohll(ctx->ccm_cb[1] & ctx->ccm_counter_mask);
		counter = htonll(counter + 1);
#else
		counter = ctx->ccm_cb[1] & ctx->ccm_counter_mask;
		counter++;
#endif	/* _LITTLE_ENDIAN */
		counter &= ctx->ccm_counter_mask;
		ctx->ccm_cb[1] =
		    (ctx->ccm_cb[1] & ~(ctx->ccm_counter_mask)) | counter;

		/*
		 * XOR encrypted counter block with the current clear block.
		 */
		xor_block(blockp, lastp);

		ctx->ccm_processed_data_len += block_size;

		if (out == NULL) {
			if (ctx->ccm_remainder_len > 0) {
				bcopy(blockp, ctx->ccm_copy_to,
				    ctx->ccm_remainder_len);
				bcopy(blockp + ctx->ccm_remainder_len, datap,
				    need);
			}
		} else {
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
		if (ctx->ccm_remainder_len != 0) {
			datap += need;
			ctx->ccm_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->ccm_remainder, remainder);
			ctx->ccm_remainder_len = remainder;
			ctx->ccm_copy_to = datap;
			goto out;
		}
		ctx->ccm_copy_to = NULL;

	} while (remainder > 0);

out:
	return (CRYPTO_SUCCESS);
}

void
calculate_ccm_mac(ccm_ctx_t *ctx, uint8_t *ccm_mac,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *))
{
	uint64_t counter;
	uint8_t *counterp, *mac_buf;
	int i;

	mac_buf = (uint8_t *)ctx->ccm_mac_buf;

	/* first counter block start with index 0 */
	counter = 0;
	ctx->ccm_cb[1] = (ctx->ccm_cb[1] & ~(ctx->ccm_counter_mask)) | counter;

	counterp = (uint8_t *)ctx->ccm_tmp;
	encrypt_block(ctx->ccm_keysched, (uint8_t *)ctx->ccm_cb, counterp);

	/* calculate XOR of MAC with first counter block */
	for (i = 0; i < ctx->ccm_mac_len; i++) {
		ccm_mac[i] = mac_buf[i] ^ counterp[i];
	}
}

/* ARGSUSED */
int
ccm_encrypt_final(ccm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *lastp, *mac_buf, *ccm_mac_p, *macp = NULL;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	size_t out_data_1_len;
	int i;

	if (out->cd_length < (ctx->ccm_remainder_len + ctx->ccm_mac_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * When we get here, the number of bytes of payload processed
	 * plus whatever data remains, if any,
	 * should be the same as the number of bytes that's being
	 * passed in the argument during init time.
	 */
	if ((ctx->ccm_processed_data_len + ctx->ccm_remainder_len)
	    != (ctx->ccm_data_len)) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	mac_buf = (uint8_t *)ctx->ccm_mac_buf;

	if (ctx->ccm_remainder_len > 0) {

		/* ccm_mac_input_buf is not used for encryption */
		macp = (uint8_t *)ctx->ccm_mac_input_buf;
		bzero(macp, block_size);

		/* copy remainder to temporary buffer */
		bcopy(ctx->ccm_remainder, macp, ctx->ccm_remainder_len);

		/* calculate the CBC MAC */
		xor_block(macp, mac_buf);
		encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);

		/* calculate the counter mode */
		lastp = (uint8_t *)ctx->ccm_tmp;
		encrypt_block(ctx->ccm_keysched, (uint8_t *)ctx->ccm_cb, lastp);

		/* XOR with counter block */
		for (i = 0; i < ctx->ccm_remainder_len; i++) {
			macp[i] ^= lastp[i];
		}
		ctx->ccm_processed_data_len += ctx->ccm_remainder_len;
	}

	/* Calculate the CCM MAC */
	ccm_mac_p = (uint8_t *)ctx->ccm_tmp;
	calculate_ccm_mac(ctx, ccm_mac_p, encrypt_block);

	crypto_init_ptrs(out, &iov_or_mp, &offset);
	crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
	    &out_data_1_len, &out_data_2,
	    ctx->ccm_remainder_len + ctx->ccm_mac_len);

	if (ctx->ccm_remainder_len > 0) {

		/* copy temporary block to where it belongs */
		if (out_data_2 == NULL) {
			/* everything will fit in out_data_1 */
			bcopy(macp, out_data_1, ctx->ccm_remainder_len);
			bcopy(ccm_mac_p, out_data_1 + ctx->ccm_remainder_len,
			    ctx->ccm_mac_len);
		} else {

			if (out_data_1_len < ctx->ccm_remainder_len) {

				size_t data_2_len_used;

				bcopy(macp, out_data_1, out_data_1_len);

				data_2_len_used = ctx->ccm_remainder_len
				    - out_data_1_len;

				bcopy((uint8_t *)macp + out_data_1_len,
				    out_data_2, data_2_len_used);
				bcopy(ccm_mac_p, out_data_2 + data_2_len_used,
				    ctx->ccm_mac_len);
			} else {
				bcopy(macp, out_data_1, out_data_1_len);
				if (out_data_1_len == ctx->ccm_remainder_len) {
					/* mac will be in out_data_2 */
					bcopy(ccm_mac_p, out_data_2,
					    ctx->ccm_mac_len);
				} else {
					size_t len_not_used = out_data_1_len -
					    ctx->ccm_remainder_len;
					/*
					 * part of mac in will be in
					 * out_data_1, part of the mac will be
					 * in out_data_2
					 */
					bcopy(ccm_mac_p,
					    out_data_1 + ctx->ccm_remainder_len,
					    len_not_used);
					bcopy(ccm_mac_p + len_not_used,
					    out_data_2,
					    ctx->ccm_mac_len - len_not_used);

				}
			}
		}
	} else {
		/* copy block to where it belongs */
		bcopy(ccm_mac_p, out_data_1, out_data_1_len);
		if (out_data_2 != NULL) {
			bcopy(ccm_mac_p + out_data_1_len, out_data_2,
			    block_size - out_data_1_len);
		}
	}
	out->cd_offset += ctx->ccm_remainder_len + ctx->ccm_mac_len;
	ctx->ccm_remainder_len = 0;
	return (CRYPTO_SUCCESS);
}

/*
 * This will only deal with decrypting the last block of the input that
 * might not be a multiple of block length.
 */
void
ccm_decrypt_incomplete_block(ccm_ctx_t *ctx,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *))
{
	uint8_t *datap, *outp, *counterp;
	int i;

	datap = (uint8_t *)ctx->ccm_remainder;
	outp = &((ctx->ccm_pt_buf)[ctx->ccm_processed_data_len]);

	counterp = (uint8_t *)ctx->ccm_tmp;
	encrypt_block(ctx->ccm_keysched, (uint8_t *)ctx->ccm_cb, counterp);

	/* XOR with counter block */
	for (i = 0; i < ctx->ccm_remainder_len; i++) {
		outp[i] = datap[i] ^ counterp[i];
	}
}

/*
 * This will decrypt the cipher text.  However, the plaintext won't be
 * returned to the caller.  It will be returned when decrypt_final() is
 * called if the MAC matches
 */
/* ARGSUSED */
int
ccm_mode_decrypt_contiguous_blocks(ccm_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t remainder = length;
	size_t need = 0;
	uint8_t *datap = (uint8_t *)data;
	uint8_t *blockp;
	uint8_t *cbp;
	uint64_t counter;
	size_t pt_len, total_decrypted_len, mac_len, pm_len, pd_len;
	uint8_t *resultp;


	pm_len = ctx->ccm_processed_mac_len;

	if (pm_len > 0) {
		uint8_t *tmp;
		/*
		 * all ciphertext has been processed, just waiting for
		 * part of the value of the mac
		 */
		if ((pm_len + length) > ctx->ccm_mac_len) {
			return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
		}
		tmp = (uint8_t *)ctx->ccm_mac_input_buf;

		bcopy(datap, tmp + pm_len, length);

		ctx->ccm_processed_mac_len += length;
		return (CRYPTO_SUCCESS);
	}

	/*
	 * If we decrypt the given data, what total amount of data would
	 * have been decrypted?
	 */
	pd_len = ctx->ccm_processed_data_len;
	total_decrypted_len = pd_len + length + ctx->ccm_remainder_len;

	if (total_decrypted_len >
	    (ctx->ccm_data_len + ctx->ccm_mac_len)) {
		return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
	}

	pt_len = ctx->ccm_data_len;

	if (total_decrypted_len > pt_len) {
		/*
		 * part of the input will be the MAC, need to isolate that
		 * to be dealt with later.  The left-over data in
		 * ccm_remainder_len from last time will not be part of the
		 * MAC.  Otherwise, it would have already been taken out
		 * when this call is made last time.
		 */
		size_t pt_part = pt_len - pd_len - ctx->ccm_remainder_len;

		mac_len = length - pt_part;

		ctx->ccm_processed_mac_len = mac_len;
		bcopy(data + pt_part, ctx->ccm_mac_input_buf, mac_len);

		if (pt_part + ctx->ccm_remainder_len < block_size) {
			/*
			 * since this is last of the ciphertext, will
			 * just decrypt with it here
			 */
			bcopy(datap, &((uint8_t *)ctx->ccm_remainder)
			    [ctx->ccm_remainder_len], pt_part);
			ctx->ccm_remainder_len += pt_part;
			ccm_decrypt_incomplete_block(ctx, encrypt_block);
			ctx->ccm_processed_data_len += ctx->ccm_remainder_len;
			ctx->ccm_remainder_len = 0;
			return (CRYPTO_SUCCESS);
		} else {
			/* let rest of the code handle this */
			length = pt_part;
		}
	} else if (length + ctx->ccm_remainder_len < block_size) {
			/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->ccm_remainder + ctx->ccm_remainder_len,
		    length);
		ctx->ccm_remainder_len += length;
		ctx->ccm_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	do {
		/* Unprocessed data from last call. */
		if (ctx->ccm_remainder_len > 0) {
			need = block_size - ctx->ccm_remainder_len;

			if (need > remainder)
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->ccm_remainder)
			    [ctx->ccm_remainder_len], need);

			blockp = (uint8_t *)ctx->ccm_remainder;
		} else {
			blockp = datap;
		}

		/* Calculate the counter mode, ccm_cb is the counter block */
		cbp = (uint8_t *)ctx->ccm_tmp;
		encrypt_block(ctx->ccm_keysched, (uint8_t *)ctx->ccm_cb, cbp);

		/*
		 * Increment counter.
		 * Counter bits are confined to the bottom 64 bits
		 */
#ifdef _LITTLE_ENDIAN
		counter = ntohll(ctx->ccm_cb[1] & ctx->ccm_counter_mask);
		counter = htonll(counter + 1);
#else
		counter = ctx->ccm_cb[1] & ctx->ccm_counter_mask;
		counter++;
#endif	/* _LITTLE_ENDIAN */
		counter &= ctx->ccm_counter_mask;
		ctx->ccm_cb[1] =
		    (ctx->ccm_cb[1] & ~(ctx->ccm_counter_mask)) | counter;

		/* XOR with the ciphertext */
		xor_block(blockp, cbp);

		/* Copy the plaintext to the "holding buffer" */
		resultp = (uint8_t *)ctx->ccm_pt_buf +
		    ctx->ccm_processed_data_len;
		copy_block(cbp, resultp);

		ctx->ccm_processed_data_len += block_size;

		ctx->ccm_lastp = blockp;

		/* Update pointer to next block of data to be processed. */
		if (ctx->ccm_remainder_len != 0) {
			datap += need;
			ctx->ccm_remainder_len = 0;
		} else {
			datap += block_size;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block */
		if (remainder > 0 && remainder < block_size) {
			bcopy(datap, ctx->ccm_remainder, remainder);
			ctx->ccm_remainder_len = remainder;
			ctx->ccm_copy_to = datap;
			if (ctx->ccm_processed_mac_len > 0) {
				/*
				 * not expecting anymore ciphertext, just
				 * compute plaintext for the remaining input
				 */
				ccm_decrypt_incomplete_block(ctx,
				    encrypt_block);
				ctx->ccm_processed_data_len += remainder;
				ctx->ccm_remainder_len = 0;
			}
			goto out;
		}
		ctx->ccm_copy_to = NULL;

	} while (remainder > 0);

out:
	return (CRYPTO_SUCCESS);
}

int
ccm_decrypt_final(ccm_ctx_t *ctx, crypto_data_t *out, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*copy_block)(uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	size_t mac_remain, pt_len;
	uint8_t *pt, *mac_buf, *macp, *ccm_mac_p;
	int rv;

	pt_len = ctx->ccm_data_len;

	/* Make sure output buffer can fit all of the plaintext */
	if (out->cd_length < pt_len) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	pt = ctx->ccm_pt_buf;
	mac_remain = ctx->ccm_processed_data_len;
	mac_buf = (uint8_t *)ctx->ccm_mac_buf;

	macp = (uint8_t *)ctx->ccm_tmp;

	while (mac_remain > 0) {

		if (mac_remain < block_size) {
			bzero(macp, block_size);
			bcopy(pt, macp, mac_remain);
			mac_remain = 0;
		} else {
			copy_block(pt, macp);
			mac_remain -= block_size;
			pt += block_size;
		}

		/* calculate the CBC MAC */
		xor_block(macp, mac_buf);
		encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);
	}

	/* Calculate the CCM MAC */
	ccm_mac_p = (uint8_t *)ctx->ccm_tmp;
	calculate_ccm_mac((ccm_ctx_t *)ctx, ccm_mac_p, encrypt_block);

	/* compare the input CCM MAC value with what we calculated */
	if (bcmp(ctx->ccm_mac_input_buf, ccm_mac_p, ctx->ccm_mac_len)) {
		/* They don't match */
		return (CRYPTO_INVALID_MAC);
	} else {
		rv = crypto_put_output_data(ctx->ccm_pt_buf, out, pt_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
		out->cd_offset += pt_len;
	}
	return (CRYPTO_SUCCESS);
}

int
ccm_validate_args(CK_AES_CCM_PARAMS *ccm_param, boolean_t is_encrypt_init)
{
	size_t macSize, nonceSize;
	uint8_t q;
	uint64_t maxValue;

	/*
	 * Check the length of the MAC.  The only valid
	 * lengths for the MAC are: 4, 6, 8, 10, 12, 14, 16
	 */
	macSize = ccm_param->ulMACSize;
	if ((macSize < 4) || (macSize > 16) || ((macSize % 2) != 0)) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	/* Check the nonce length.  Valid values are 7, 8, 9, 10, 11, 12, 13 */
	nonceSize = ccm_param->ulNonceSize;
	if ((nonceSize < 7) || (nonceSize > 13)) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	/* q is the length of the field storing the length, in bytes */
	q = (uint8_t)((15 - nonceSize) & 0xFF);


	/*
	 * If it is decrypt, need to make sure size of ciphertext is at least
	 * bigger than MAC len
	 */
	if ((!is_encrypt_init) && (ccm_param->ulDataSize < macSize)) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	/*
	 * Check to make sure the length of the payload is within the
	 * range of values allowed by q
	 */
	if (q < 8) {
		maxValue = (1ULL << (q * 8)) - 1;
	} else {
		maxValue = ULONG_MAX;
	}

	if (ccm_param->ulDataSize > maxValue) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}
	return (CRYPTO_SUCCESS);
}

/*
 * Format the first block used in CBC-MAC (B0) and the initial counter
 * block based on formatting functions and counter generation functions
 * specified in RFC 3610 and NIST publication 800-38C, appendix A
 *
 * b0 is the first block used in CBC-MAC
 * cb0 is the first counter block
 *
 * It's assumed that the arguments b0 and cb0 are preallocated AES blocks
 *
 */
static void
ccm_format_initial_blocks(uchar_t *nonce, ulong_t nonceSize,
    ulong_t authDataSize, uint8_t *b0, ccm_ctx_t *aes_ctx)
{
	uint64_t payloadSize;
	uint8_t t, q, have_adata = 0;
	size_t limit;
	int i, j, k;
	uint64_t mask = 0;
	uint8_t *cb;

	q = (uint8_t)((15 - nonceSize) & 0xFF);
	t = (uint8_t)((aes_ctx->ccm_mac_len) & 0xFF);

	/* Construct the first octet of b0 */
	if (authDataSize > 0) {
		have_adata = 1;
	}
	b0[0] = (have_adata << 6) | (((t - 2)  / 2) << 3) | (q - 1);

	/* copy the nonce value into b0 */
	bcopy(nonce, &(b0[1]), nonceSize);

	/* store the length of the payload into b0 */
	bzero(&(b0[1+nonceSize]), q);

	payloadSize = aes_ctx->ccm_data_len;
	limit = 8 < q ? 8 : q;

	for (i = 0, j = 0, k = 15; i < limit; i++, j += 8, k--) {
		b0[k] = (uint8_t)((payloadSize >> j) & 0xFF);
	}

	/* format the counter block */

	cb = (uint8_t *)aes_ctx->ccm_cb;

	cb[0] = 0x07 & (q-1); /* first byte */

	/* copy the nonce value into the counter block */
	bcopy(nonce, &(cb[1]), nonceSize);

	bzero(&(cb[1+nonceSize]), q);

	/* Create the mask for the counter field based on the size of nonce */
	q <<= 3;
	while (q-- > 0) {
		mask |= (1ULL << q);
	}

#ifdef _LITTLE_ENDIAN
	mask = htonll(mask);
#endif
	aes_ctx->ccm_counter_mask = mask;

	/*
	 * During calculation, we start using counter block 1, we will
	 * set it up right here.
	 * We can just set the last byte to have the value 1, because
	 * even with the biggest nonce of 13, the last byte of the
	 * counter block will be used for the counter value.
	 */
	cb[15] = 0x01;
}

/*
 * Encode the length of the associated data as
 * specified in RFC 3610 and NIST publication 800-38C, appendix A
 */
static void
encode_adata_len(ulong_t auth_data_len, uint8_t *encoded, size_t *encoded_len)
{
#ifdef UNALIGNED_POINTERS_PERMITTED
	uint32_t	*lencoded_ptr;
#ifdef _LP64
	uint64_t	*llencoded_ptr;
#endif
#endif	/* UNALIGNED_POINTERS_PERMITTED */

	if (auth_data_len < ((1ULL<<16) - (1ULL<<8))) {
		/* 0 < a < (2^16-2^8) */
		*encoded_len = 2;
		encoded[0] = (auth_data_len & 0xff00) >> 8;
		encoded[1] = auth_data_len & 0xff;

	} else if ((auth_data_len >= ((1ULL<<16) - (1ULL<<8))) &&
	    (auth_data_len < (1ULL << 31))) {
		/* (2^16-2^8) <= a < 2^32 */
		*encoded_len = 6;
		encoded[0] = 0xff;
		encoded[1] = 0xfe;
#ifdef UNALIGNED_POINTERS_PERMITTED
		lencoded_ptr = (uint32_t *)&encoded[2];
		*lencoded_ptr = htonl(auth_data_len);
#else
		encoded[2] = (auth_data_len & 0xff000000) >> 24;
		encoded[3] = (auth_data_len & 0xff0000) >> 16;
		encoded[4] = (auth_data_len & 0xff00) >> 8;
		encoded[5] = auth_data_len & 0xff;
#endif	/* UNALIGNED_POINTERS_PERMITTED */

#ifdef _LP64
	} else {
		/* 2^32 <= a < 2^64 */
		*encoded_len = 10;
		encoded[0] = 0xff;
		encoded[1] = 0xff;
#ifdef UNALIGNED_POINTERS_PERMITTED
		llencoded_ptr = (uint64_t *)&encoded[2];
		*llencoded_ptr = htonl(auth_data_len);
#else
		encoded[2] = (auth_data_len & 0xff00000000000000) >> 56;
		encoded[3] = (auth_data_len & 0xff000000000000) >> 48;
		encoded[4] = (auth_data_len & 0xff0000000000) >> 40;
		encoded[5] = (auth_data_len & 0xff00000000) >> 32;
		encoded[6] = (auth_data_len & 0xff000000) >> 24;
		encoded[7] = (auth_data_len & 0xff0000) >> 16;
		encoded[8] = (auth_data_len & 0xff00) >> 8;
		encoded[9] = auth_data_len & 0xff;
#endif	/* UNALIGNED_POINTERS_PERMITTED */
#endif	/* _LP64 */
	}
}

/*
 * The following function should be call at encrypt or decrypt init time
 * for AES CCM mode.
 */
int
ccm_init(ccm_ctx_t *ctx, unsigned char *nonce, size_t nonce_len,
    unsigned char *auth_data, size_t auth_data_len, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *mac_buf, *datap, *ivp, *authp;
	size_t remainder, processed;
	uint8_t encoded_a[10]; /* max encoded auth data length is 10 octets */
	size_t encoded_a_len = 0;

	mac_buf = (uint8_t *)&(ctx->ccm_mac_buf);

	/*
	 * Format the 1st block for CBC-MAC and construct the
	 * 1st counter block.
	 *
	 * aes_ctx->ccm_iv is used for storing the counter block
	 * mac_buf will store b0 at this time.
	 */
	ccm_format_initial_blocks(nonce, nonce_len,
	    auth_data_len, mac_buf, ctx);

	/* The IV for CBC MAC for AES CCM mode is always zero */
	ivp = (uint8_t *)ctx->ccm_tmp;
	bzero(ivp, block_size);

	xor_block(ivp, mac_buf);

	/* encrypt the nonce */
	encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);

	/* take care of the associated data, if any */
	if (auth_data_len == 0) {
		return (CRYPTO_SUCCESS);
	}

	encode_adata_len(auth_data_len, encoded_a, &encoded_a_len);

	remainder = auth_data_len;

	/* 1st block: it contains encoded associated data, and some data */
	authp = (uint8_t *)ctx->ccm_tmp;
	bzero(authp, block_size);
	bcopy(encoded_a, authp, encoded_a_len);
	processed = block_size - encoded_a_len;
	if (processed > auth_data_len) {
		/* in case auth_data is very small */
		processed = auth_data_len;
	}
	bcopy(auth_data, authp+encoded_a_len, processed);
	/* xor with previous buffer */
	xor_block(authp, mac_buf);
	encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);
	remainder -= processed;
	if (remainder == 0) {
		/* a small amount of associated data, it's all done now */
		return (CRYPTO_SUCCESS);
	}

	do {
		if (remainder < block_size) {
			/*
			 * There's not a block full of data, pad rest of
			 * buffer with zero
			 */
			bzero(authp, block_size);
			bcopy(&(auth_data[processed]), authp, remainder);
			datap = (uint8_t *)authp;
			remainder = 0;
		} else {
			datap = (uint8_t *)(&(auth_data[processed]));
			processed += block_size;
			remainder -= block_size;
		}

		xor_block(datap, mac_buf);
		encrypt_block(ctx->ccm_keysched, mac_buf, mac_buf);

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}

int
ccm_init_ctx(ccm_ctx_t *ccm_ctx, char *param, int kmflag,
    boolean_t is_encrypt_init, size_t block_size,
    int (*encrypt_block)(const void *, const uint8_t *, uint8_t *),
    void (*xor_block)(uint8_t *, uint8_t *))
{
	int rv;
	CK_AES_CCM_PARAMS *ccm_param;

	if (param != NULL) {
		ccm_param = (CK_AES_CCM_PARAMS *)param;

		if ((rv = ccm_validate_args(ccm_param,
		    is_encrypt_init)) != 0) {
			return (rv);
		}

		ccm_ctx->ccm_mac_len = ccm_param->ulMACSize;
		if (is_encrypt_init) {
			ccm_ctx->ccm_data_len = ccm_param->ulDataSize;
		} else {
			ccm_ctx->ccm_data_len =
			    ccm_param->ulDataSize - ccm_ctx->ccm_mac_len;
			ccm_ctx->ccm_processed_mac_len = 0;
		}
		ccm_ctx->ccm_processed_data_len = 0;

		ccm_ctx->ccm_flags |= CCM_MODE;
	} else {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto out;
	}

	if (ccm_init(ccm_ctx, ccm_param->nonce, ccm_param->ulNonceSize,
	    ccm_param->authData, ccm_param->ulAuthDataSize, block_size,
	    encrypt_block, xor_block) != 0) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto out;
	}
	if (!is_encrypt_init) {
		/* allocate buffer for storing decrypted plaintext */
		ccm_ctx->ccm_pt_buf = vmem_alloc(ccm_ctx->ccm_data_len,
		    kmflag);
		if (ccm_ctx->ccm_pt_buf == NULL) {
			rv = CRYPTO_HOST_MEMORY;
		}
	}
out:
	return (rv);
}

void *
ccm_alloc_ctx(int kmflag)
{
	ccm_ctx_t *ccm_ctx;

	if ((ccm_ctx = kmem_zalloc(sizeof (ccm_ctx_t), kmflag)) == NULL)
		return (NULL);

	ccm_ctx->ccm_flags = CCM_MODE;
	return (ccm_ctx);
}
