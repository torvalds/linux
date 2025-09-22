/*	$OpenBSD: ieee80211_crypto_bip.c,v 1.10 2018/11/09 14:14:31 claudio Exp $	*/

/*-
 * Copyright (c) 2008 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This code implements the Broadcast/Multicast Integrity Protocol (BIP)
 * defined in IEEE P802.11w/D7.0 section 8.3.4.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_priv.h>

#include <crypto/aes.h>
#include <crypto/cmac.h>

/* BIP software crypto context */
struct ieee80211_bip_ctx {
	AES_CMAC_CTX	cmac;
};

/*
 * Initialize software crypto context.  This function can be overridden
 * by drivers doing hardware crypto.
 */
int
ieee80211_bip_set_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	struct ieee80211_bip_ctx *ctx;

	ctx = malloc(sizeof(*ctx), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ctx == NULL)
		return ENOMEM;
	AES_CMAC_SetKey(&ctx->cmac, k->k_key);
	k->k_priv = ctx;
	return 0;
}

void
ieee80211_bip_delete_key(struct ieee80211com *ic, struct ieee80211_key *k)
{
	if (k->k_priv != NULL) {
		explicit_bzero(k->k_priv, sizeof(struct ieee80211_bip_ctx));
		free(k->k_priv, M_DEVBUF, sizeof(struct ieee80211_bip_ctx));
	}
	k->k_priv = NULL;
}

/* pseudo-header used for BIP MIC computation */
struct ieee80211_bip_frame {
	u_int8_t	i_fc[2];
	u_int8_t	i_addr1[IEEE80211_ADDR_LEN];
	u_int8_t	i_addr2[IEEE80211_ADDR_LEN];
	u_int8_t	i_addr3[IEEE80211_ADDR_LEN];
} __packed;

struct mbuf *
ieee80211_bip_encap(struct ieee80211com *ic, struct mbuf *m0,
    struct ieee80211_key *k)
{
	struct ieee80211_bip_ctx *ctx = k->k_priv;
	struct ieee80211_bip_frame aad;
	struct ieee80211_frame *wh;
	u_int8_t *mmie, mic[AES_CMAC_DIGEST_LENGTH];
	struct mbuf *m;

	wh = mtod(m0, struct ieee80211_frame *);
	KASSERT((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_MGT);
	/* clear Protected bit from group management frames */
	wh->i_fc[1] &= ~IEEE80211_FC1_PROTECTED;

	/* construct AAD (additional authenticated data) */
	aad.i_fc[0] = wh->i_fc[0];
	aad.i_fc[1] = wh->i_fc[1] & ~(IEEE80211_FC1_RETRY |
	    IEEE80211_FC1_PWR_MGT | IEEE80211_FC1_MORE_DATA);
	/* XXX 11n may require clearing the Order bit too */
	IEEE80211_ADDR_COPY(aad.i_addr1, wh->i_addr1);
	IEEE80211_ADDR_COPY(aad.i_addr2, wh->i_addr2);
	IEEE80211_ADDR_COPY(aad.i_addr3, wh->i_addr3);

	AES_CMAC_Init(&ctx->cmac);
	AES_CMAC_Update(&ctx->cmac, (u_int8_t *)&aad, sizeof aad);
	AES_CMAC_Update(&ctx->cmac, (u_int8_t *)&wh[1],
	    m0->m_len - sizeof(*wh));

	m = m0;
	/* reserve trailing space for MMIE */
	if (m_trailingspace(m) < IEEE80211_MMIE_LEN) {
		MGET(m->m_next, M_DONTWAIT, m->m_type);
		if (m->m_next == NULL)
			goto nospace;
		m = m->m_next;
		m->m_len = 0;
	}

	/* construct Management MIC IE */
	mmie = mtod(m, u_int8_t *) + m->m_len;
	mmie[0] = IEEE80211_ELEMID_MMIE;
	mmie[1] = 16;
	LE_WRITE_2(&mmie[2], k->k_id);
	LE_WRITE_6(&mmie[4], k->k_tsc);
	memset(&mmie[10], 0, 8);	/* MMIE MIC field set to 0 */

	AES_CMAC_Update(&ctx->cmac, mmie, IEEE80211_MMIE_LEN);
	AES_CMAC_Final(mic, &ctx->cmac);
	/* truncate AES-128-CMAC to 64-bit */
	memcpy(&mmie[10], mic, 8);

	m->m_len += IEEE80211_MMIE_LEN;
	m0->m_pkthdr.len += IEEE80211_MMIE_LEN;

	k->k_tsc++;

	return m0;
 nospace:
	ic->ic_stats.is_tx_nombuf++;
	m_freem(m0);
	return NULL;
}

struct mbuf *
ieee80211_bip_decap(struct ieee80211com *ic, struct mbuf *m0,
    struct ieee80211_key *k)
{
	struct ieee80211_bip_ctx *ctx = k->k_priv;
	struct ieee80211_frame *wh;
	struct ieee80211_bip_frame aad;
	u_int8_t *mmie, mic0[8], mic[AES_CMAC_DIGEST_LENGTH];
	u_int64_t ipn;

	wh = mtod(m0, struct ieee80211_frame *);
	KASSERT((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
	    IEEE80211_FC0_TYPE_MGT);

	/*
	 * It is assumed that management frames are contiguous and that
	 * the mbuf length has already been checked to contain at least
	 * a header and a MMIE (checked in ieee80211_decrypt()).
	 */
	KASSERT(m0->m_len >= sizeof(*wh) + IEEE80211_MMIE_LEN);
	mmie = mtod(m0, u_int8_t *) + m0->m_len - IEEE80211_MMIE_LEN;

	ipn = LE_READ_6(&mmie[4]);
	if (ipn <= k->k_mgmt_rsc) {
		/* replayed frame, discard */
		ic->ic_stats.is_cmac_replays++;
		m_freem(m0);
		return NULL;
	}

	/* save and mask MMIE MIC field to 0 */
	memcpy(mic0, &mmie[10], 8);
	memset(&mmie[10], 0, 8);

	/* construct AAD (additional authenticated data) */
	aad.i_fc[0] = wh->i_fc[0];
	aad.i_fc[1] = wh->i_fc[1] & ~(IEEE80211_FC1_RETRY |
	    IEEE80211_FC1_PWR_MGT | IEEE80211_FC1_MORE_DATA);
	/* XXX 11n may require clearing the Order bit too */
	IEEE80211_ADDR_COPY(aad.i_addr1, wh->i_addr1);
	IEEE80211_ADDR_COPY(aad.i_addr2, wh->i_addr2);
	IEEE80211_ADDR_COPY(aad.i_addr3, wh->i_addr3);

	/* compute MIC */
	AES_CMAC_Init(&ctx->cmac);
	AES_CMAC_Update(&ctx->cmac, (u_int8_t *)&aad, sizeof aad);
	AES_CMAC_Update(&ctx->cmac, (u_int8_t *)&wh[1],
	    m0->m_len - sizeof(*wh));
	AES_CMAC_Final(mic, &ctx->cmac);

	/* check that MIC matches the one in MMIE */
	if (timingsafe_bcmp(mic, mic0, 8) != 0) {
		ic->ic_stats.is_cmac_icv_errs++;
		m_freem(m0);
		return NULL;
	}
	/*
	 * There is no need to trim the MMIE from the mbuf since it is
	 * an information element and will be ignored by upper layers.
	 * We do it anyway as it is cheap to do it here and because it
	 * may be confused with fixed fields by upper layers.
	 */
	m_adj(m0, -IEEE80211_MMIE_LEN);

	/* update last seen packet number (MIC is validated) */
	k->k_mgmt_rsc = ipn;

	return m0;
}
