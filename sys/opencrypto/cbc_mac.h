/*
 * Copyright (c) 2014 The FreeBSD Foundation
 * Copyright (c) 2018, iXsystems Inc.
 * All rights reserved.
 *
 * This software was developed by Sean Eric Fagan, with lots of references
 * to existing AES-CCM (gmac) code.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 *
 */

#ifndef _CBC_CCM_H
# define _CBC_CCM_H

# include <sys/types.h>
# include <crypto/rijndael/rijndael.h>

# define CCM_CBC_BLOCK_LEN	16	/* 128 bits */
# define CCM_CBC_MAX_DIGEST_LEN	16
# define CCM_CBC_MIN_DIGEST_LEN	4

/*
 * This is the authentication context structure;
 * the encryption one is similar.
 */
struct aes_cbc_mac_ctx {
	uint64_t	authDataLength, authDataCount;
	uint64_t	cryptDataLength, cryptDataCount;
	int		blockIndex;
	uint8_t		staging_block[CCM_CBC_BLOCK_LEN];
	uint8_t		block[CCM_CBC_BLOCK_LEN];
	const uint8_t	*nonce;
	int		nonceLength;	/* This one is in bytes, not bits! */
	/* AES state data */
	int		rounds;
	uint32_t	keysched[4*(RIJNDAEL_MAXNR+1)];
};

void AES_CBC_MAC_Init(struct aes_cbc_mac_ctx *);
void AES_CBC_MAC_Setkey(struct aes_cbc_mac_ctx *, const uint8_t *, uint16_t);
void AES_CBC_MAC_Reinit(struct aes_cbc_mac_ctx *, const uint8_t *, uint16_t);
int AES_CBC_MAC_Update(struct aes_cbc_mac_ctx *, const uint8_t *, uint16_t);
void AES_CBC_MAC_Final(uint8_t *, struct aes_cbc_mac_ctx *);

#endif /* _CBC_CCM_H */
