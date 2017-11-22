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
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Utility routine to copy a buffer to a crypto_data structure.
 */

/*
 * Utility routine to apply the command, 'cmd', to the
 * data in the uio structure.
 */
int
crypto_uio_data(crypto_data_t *data, uchar_t *buf, int len, cmd_type_t cmd,
    void *digest_ctx, void (*update)(void))
{
	uio_t *uiop = data->cd_uio;
	off_t offset = data->cd_offset;
	size_t length = len;
	uint_t vec_idx;
	size_t cur_len;
	uchar_t *datap;

	ASSERT(data->cd_format == CRYPTO_DATA_UIO);
	if (uiop->uio_segflg != UIO_SYSSPACE) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Jump to the first iovec containing data to be
	 * processed.
	 */
	for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
	    offset >= uiop->uio_iov[vec_idx].iov_len;
	    offset -= uiop->uio_iov[vec_idx++].iov_len)
		;

	if (vec_idx == uiop->uio_iovcnt) {
		/*
		 * The caller specified an offset that is larger than
		 * the total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	while (vec_idx < uiop->uio_iovcnt && length > 0) {
		cur_len = MIN(uiop->uio_iov[vec_idx].iov_len -
		    offset, length);

		datap = (uchar_t *)(uiop->uio_iov[vec_idx].iov_base +
		    offset);
		switch (cmd) {
		case COPY_FROM_DATA:
			bcopy(datap, buf, cur_len);
			buf += cur_len;
			break;
		case COPY_TO_DATA:
			bcopy(buf, datap, cur_len);
			buf += cur_len;
			break;
		case COMPARE_TO_DATA:
			if (bcmp(datap, buf, cur_len))
				return (CRYPTO_SIGNATURE_INVALID);
			buf += cur_len;
			break;
		case MD5_DIGEST_DATA:
		case SHA1_DIGEST_DATA:
		case SHA2_DIGEST_DATA:
		case GHASH_DATA:
			return (CRYPTO_ARGUMENTS_BAD);
		}

		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == uiop->uio_iovcnt && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed.
		 */
		switch (cmd) {
		case COPY_TO_DATA:
			data->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		default:
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	return (CRYPTO_SUCCESS);
}

int
crypto_put_output_data(uchar_t *buf, crypto_data_t *output, int len)
{
	switch (output->cd_format) {
	case CRYPTO_DATA_RAW:
		if (output->cd_raw.iov_len < len) {
			output->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		bcopy(buf, (uchar_t *)(output->cd_raw.iov_base +
		    output->cd_offset), len);
		break;

	case CRYPTO_DATA_UIO:
		return (crypto_uio_data(output, buf, len,
		    COPY_TO_DATA, NULL, NULL));
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	return (CRYPTO_SUCCESS);
}

int
crypto_update_iov(void *ctx, crypto_data_t *input, crypto_data_t *output,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *))
{
	common_ctx_t *common_ctx = ctx;
	int rv;

	if (input->cd_miscdata != NULL) {
		copy_block((uint8_t *)input->cd_miscdata,
		    &common_ctx->cc_iv[0]);
	}

	if (input->cd_raw.iov_len < input->cd_length)
		return (CRYPTO_ARGUMENTS_BAD);

	rv = (cipher)(ctx, input->cd_raw.iov_base + input->cd_offset,
	    input->cd_length, (input == output) ? NULL : output);

	return (rv);
}

int
crypto_update_uio(void *ctx, crypto_data_t *input, crypto_data_t *output,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *))
{
	common_ctx_t *common_ctx = ctx;
	uio_t *uiop = input->cd_uio;
	off_t offset = input->cd_offset;
	size_t length = input->cd_length;
	uint_t vec_idx;
	size_t cur_len;

	if (input->cd_miscdata != NULL) {
		copy_block((uint8_t *)input->cd_miscdata,
		    &common_ctx->cc_iv[0]);
	}

	if (input->cd_uio->uio_segflg != UIO_SYSSPACE) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Jump to the first iovec containing data to be
	 * processed.
	 */
	for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
	    offset >= uiop->uio_iov[vec_idx].iov_len;
	    offset -= uiop->uio_iov[vec_idx++].iov_len)
		;
	if (vec_idx == uiop->uio_iovcnt) {
		/*
		 * The caller specified an offset that is larger than the
		 * total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now process the iovecs.
	 */
	while (vec_idx < uiop->uio_iovcnt && length > 0) {
		cur_len = MIN(uiop->uio_iov[vec_idx].iov_len -
		    offset, length);

		(cipher)(ctx, uiop->uio_iov[vec_idx].iov_base + offset,
		    cur_len, (input == output) ? NULL : output);

		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == uiop->uio_iovcnt && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed, i.e.
		 * The caller requested to digest more data than it provided.
		 */

		return (CRYPTO_DATA_LEN_RANGE);
	}

	return (CRYPTO_SUCCESS);
}
