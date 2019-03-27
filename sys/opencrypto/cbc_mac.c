/*
 * Copyright (c) 2018-2019 iXsystems Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/endian.h>
#include <opencrypto/cbc_mac.h>
#include <opencrypto/xform_auth.h>

/*
 * Given two CCM_CBC_BLOCK_LEN blocks, xor
 * them into dst, and then encrypt dst.
 */
static void
xor_and_encrypt(struct aes_cbc_mac_ctx *ctx,
		const uint8_t *src, uint8_t *dst)
{
	const uint64_t *b1;
	uint64_t *b2;
	uint64_t temp_block[CCM_CBC_BLOCK_LEN/sizeof(uint64_t)];

	b1 = (const uint64_t*)src;
	b2 = (uint64_t*)dst;

	for (size_t count = 0;
	     count < CCM_CBC_BLOCK_LEN/sizeof(uint64_t);
	     count++) {
		temp_block[count] = b1[count] ^ b2[count];
	}
	rijndaelEncrypt(ctx->keysched, ctx->rounds, (void*)temp_block, dst);
}

void
AES_CBC_MAC_Init(struct aes_cbc_mac_ctx *ctx)
{
	bzero(ctx, sizeof(*ctx));
}

void
AES_CBC_MAC_Setkey(struct aes_cbc_mac_ctx *ctx, const uint8_t *key, uint16_t klen)
{
	ctx->rounds = rijndaelKeySetupEnc(ctx->keysched, key, klen * 8);
}

/*
 * This is called to set the nonce, aka IV.
 * Before this call, the authDataLength and cryptDataLength fields
 * MUST have been set.  Sadly, there's no way to return an error.
 *
 * The CBC-MAC algorithm requires that the first block contain the
 * nonce, as well as information about the sizes and lengths involved.
 */
void
AES_CBC_MAC_Reinit(struct aes_cbc_mac_ctx *ctx, const uint8_t *nonce, uint16_t nonceLen)
{
	uint8_t b0[CCM_CBC_BLOCK_LEN];
	uint8_t *bp = b0, flags = 0;
	uint8_t L = 0;
	uint64_t dataLength = ctx->cryptDataLength;
	
	KASSERT(ctx->authDataLength != 0 || ctx->cryptDataLength != 0,
	    ("Auth Data and Data lengths cannot both be 0"));

	KASSERT(nonceLen >= 7 && nonceLen <= 13,
	    ("nonceLen must be between 7 and 13 bytes"));

	ctx->nonce = nonce;
	ctx->nonceLength = nonceLen;
	
	ctx->authDataCount = 0;
	ctx->blockIndex = 0;
	explicit_bzero(ctx->staging_block, sizeof(ctx->staging_block));
	
	/*
	 * Need to determine the L field value.  This is the number of
	 * bytes needed to specify the length of the message; the length
	 * is whatever is left in the 16 bytes after specifying flags and
	 * the nonce.
	 */
	L = 15 - nonceLen;
	
	flags = ((ctx->authDataLength > 0) << 6) +
	    (((AES_CBC_MAC_HASH_LEN - 2) / 2) << 3) +
	    L - 1;
	/*
	 * Now we need to set up the first block, which has flags, nonce,
	 * and the message length.
	 */
	b0[0] = flags;
	bcopy(nonce, b0 + 1, nonceLen);
	bp = b0 + 1 + nonceLen;

	/* Need to copy L' [aka L-1] bytes of cryptDataLength */
	for (uint8_t *dst = b0 + sizeof(b0) - 1; dst >= bp; dst--) {
		*dst = dataLength;
		dataLength >>= 8;
	}
	/* Now need to encrypt b0 */
	rijndaelEncrypt(ctx->keysched, ctx->rounds, b0, ctx->block);
	/* If there is auth data, we need to set up the staging block */
	if (ctx->authDataLength) {
		size_t addLength;
		if (ctx->authDataLength < ((1<<16) - (1<<8))) {
			uint16_t sizeVal = htobe16(ctx->authDataLength);
			bcopy(&sizeVal, ctx->staging_block, sizeof(sizeVal));
			addLength = sizeof(sizeVal);
		} else if (ctx->authDataLength < (1ULL<<32)) {
			uint32_t sizeVal = htobe32(ctx->authDataLength);
			ctx->staging_block[0] = 0xff;
			ctx->staging_block[1] = 0xfe;
			bcopy(&sizeVal, ctx->staging_block+2, sizeof(sizeVal));
			addLength = 2 + sizeof(sizeVal);
		} else {
			uint64_t sizeVal = htobe64(ctx->authDataLength);
			ctx->staging_block[0] = 0xff;
			ctx->staging_block[1] = 0xff;
			bcopy(&sizeVal, ctx->staging_block+2, sizeof(sizeVal));
			addLength = 2 + sizeof(sizeVal);
		}
		ctx->blockIndex = addLength;
		/*
		 * The length descriptor goes into the AAD buffer, so we
		 * need to account for it.
		 */
		ctx->authDataLength += addLength;
		ctx->authDataCount = addLength;
	}
}

int
AES_CBC_MAC_Update(struct aes_cbc_mac_ctx *ctx, const uint8_t *data,
    uint16_t length)
{
	size_t copy_amt;
	
	/*
	 * This will be called in one of two phases:
	 * (1)  Applying authentication data, or
	 * (2)  Applying the payload data.
	 *
	 * Because CBC-MAC puts the authentication data size before the
	 * data, subsequent calls won't be block-size-aligned.  Which
	 * complicates things a fair bit.
	 *
	 * The payload data doesn't have that problem.
	 */
				
	if (ctx->authDataCount < ctx->authDataLength) {
		/*
		 * We need to process data as authentication data.
		 * Since we may be out of sync, we may also need
		 * to pad out the staging block.
		 */
		const uint8_t *ptr = data;
		while (length > 0) {

			copy_amt = MIN(length,
			    sizeof(ctx->staging_block) - ctx->blockIndex);

			bcopy(ptr, ctx->staging_block + ctx->blockIndex,
			    copy_amt);
			ptr += copy_amt;
			length -= copy_amt;
			ctx->authDataCount += copy_amt;
			ctx->blockIndex += copy_amt;
			ctx->blockIndex %= sizeof(ctx->staging_block);

			if (ctx->blockIndex == 0 ||
			    ctx->authDataCount == ctx->authDataLength) {
				/*
				 * We're done with this block, so we
				 * xor staging_block with block, and then
				 * encrypt it.
				 */
				xor_and_encrypt(ctx, ctx->staging_block, ctx->block);
				bzero(ctx->staging_block, sizeof(ctx->staging_block));
				ctx->blockIndex = 0;
				if (ctx->authDataCount >= ctx->authDataLength)
					break;
			}
		}
		/*
		 * We'd like to be able to check length == 0 and return
		 * here, but the way OCF calls us, length is always
		 * blksize (16, in this case).  So we have to count on
		 * the fact that OCF calls us separately for the AAD and
		 * for the real data.
		 */
		return (0);
	}
	/*
	 * If we're here, then we're encoding payload data.
	 * This is marginally easier, except that _Update can
	 * be called with non-aligned update lengths. As a result,
	 * we still need to use the staging block.
	 */
	KASSERT((length + ctx->cryptDataCount) <= ctx->cryptDataLength,
	    ("More encryption data than allowed"));

	while (length) {
		uint8_t *ptr;
		
		copy_amt = MIN(sizeof(ctx->staging_block) - ctx->blockIndex,
		    length);
		ptr = ctx->staging_block + ctx->blockIndex;
		bcopy(data, ptr, copy_amt);
		data += copy_amt;
		ctx->blockIndex += copy_amt;
		ctx->cryptDataCount += copy_amt;
		length -= copy_amt;
		if (ctx->blockIndex == sizeof(ctx->staging_block)) {
			/* We've got a full block */
			xor_and_encrypt(ctx, ctx->staging_block, ctx->block);
			ctx->blockIndex = 0;
			bzero(ctx->staging_block, sizeof(ctx->staging_block));
		}
	}
	return (0);
}

void
AES_CBC_MAC_Final(uint8_t *buf, struct aes_cbc_mac_ctx *ctx)
{
	uint8_t s0[CCM_CBC_BLOCK_LEN];
	
	/*
	 * We first need to check to see if we've got any data
	 * left over to encrypt.
	 */
	if (ctx->blockIndex != 0) {
		xor_and_encrypt(ctx, ctx->staging_block, ctx->block);
		ctx->cryptDataCount += ctx->blockIndex;
		ctx->blockIndex = 0;
		explicit_bzero(ctx->staging_block, sizeof(ctx->staging_block));
	}
	bzero(s0, sizeof(s0));
	s0[0] = (15 - ctx->nonceLength) - 1;
	bcopy(ctx->nonce, s0 + 1, ctx->nonceLength);
	rijndaelEncrypt(ctx->keysched, ctx->rounds, s0, s0);
	for (size_t indx = 0; indx < AES_CBC_MAC_HASH_LEN; indx++)
		buf[indx] = ctx->block[indx] ^ s0[indx];
	explicit_bzero(s0, sizeof(s0));
}
