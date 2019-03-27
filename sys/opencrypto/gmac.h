/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by John-Mark Gurney under
 * the sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
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

#ifndef _GMAC_H_
#define _GMAC_H_

#include "gfmult.h"
#include <crypto/rijndael/rijndael.h>

#define	GMAC_BLOCK_LEN	16
#define	GMAC_DIGEST_LEN	16

struct aes_gmac_ctx {
	struct gf128table4	ghashtbl;
	struct gf128		hash;
	uint32_t		keysched[4*(RIJNDAEL_MAXNR + 1)];
	uint8_t			counter[GMAC_BLOCK_LEN];
	int			rounds;
};

void AES_GMAC_Init(struct aes_gmac_ctx *);
void AES_GMAC_Setkey(struct aes_gmac_ctx *, const uint8_t *, uint16_t);
void AES_GMAC_Reinit(struct aes_gmac_ctx *, const uint8_t *, uint16_t);
int AES_GMAC_Update(struct aes_gmac_ctx *, const uint8_t *, uint16_t);
void AES_GMAC_Final(uint8_t [GMAC_DIGEST_LEN], struct aes_gmac_ctx *);

#endif /* _GMAC_H_ */
