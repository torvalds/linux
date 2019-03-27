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

#include <sys/types.h>
#include <sys/systm.h>
#include <opencrypto/gfmult.h>
#include <opencrypto/gmac.h>

void
AES_GMAC_Init(struct aes_gmac_ctx *agc)
{

	bzero(agc, sizeof *agc);
}

void
AES_GMAC_Setkey(struct aes_gmac_ctx *agc, const uint8_t *key, uint16_t klen)
{
	const uint8_t zeros[GMAC_BLOCK_LEN] = {};
	struct gf128 h;
	uint8_t hbuf[GMAC_BLOCK_LEN];

	agc->rounds = rijndaelKeySetupEnc(agc->keysched, key, klen * 8);

	rijndaelEncrypt(agc->keysched, agc->rounds, zeros, hbuf);

	h = gf128_read(hbuf);
	gf128_genmultable4(h, &agc->ghashtbl);

	explicit_bzero(&h, sizeof h);
	explicit_bzero(hbuf, sizeof hbuf);
}

void
AES_GMAC_Reinit(struct aes_gmac_ctx *agc, const uint8_t *iv, uint16_t ivlen)
{

	KASSERT(ivlen <= sizeof agc->counter, ("passed ivlen too large!"));
	bcopy(iv, agc->counter, ivlen);
}

int
AES_GMAC_Update(struct aes_gmac_ctx *agc, const uint8_t *data, uint16_t len)
{
	struct gf128 v;
	uint8_t buf[GMAC_BLOCK_LEN] = {};
	int i;

	v = agc->hash;

	while (len > 0) {
		if (len >= 4*GMAC_BLOCK_LEN) {
			i = 4*GMAC_BLOCK_LEN;
			v = gf128_mul4b(v, data, &agc->ghashtbl);
		} else if (len >= GMAC_BLOCK_LEN) {
			i = GMAC_BLOCK_LEN;
			v = gf128_add(v, gf128_read(data));
			v = gf128_mul(v, &agc->ghashtbl.tbls[0]);
		} else {
			i = len;
			bcopy(data, buf, i);
			v = gf128_add(v, gf128_read(&buf[0]));
			v = gf128_mul(v, &agc->ghashtbl.tbls[0]);
			explicit_bzero(buf, sizeof buf);
		}
		len -= i;
		data += i;
	}

	agc->hash = v;
	explicit_bzero(&v, sizeof v);

	return (0);
}

void
AES_GMAC_Final(uint8_t digest[GMAC_DIGEST_LEN], struct aes_gmac_ctx *agc)
{
	uint8_t enccntr[GMAC_BLOCK_LEN];
	struct gf128 a;

	/* XXX - zero additional bytes? */
	agc->counter[GMAC_BLOCK_LEN - 1] = 1;

	rijndaelEncrypt(agc->keysched, agc->rounds, agc->counter, enccntr);
	a = gf128_add(agc->hash, gf128_read(enccntr));
	gf128_write(a, digest);

	explicit_bzero(enccntr, sizeof enccntr);
}
