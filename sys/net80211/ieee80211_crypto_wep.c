/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
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

/*
 * IEEE 802.11 WEP crypto support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/endian.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

static	void *wep_attach(struct ieee80211vap *, struct ieee80211_key *);
static	void wep_detach(struct ieee80211_key *);
static	int wep_setkey(struct ieee80211_key *);
static	void wep_setiv(struct ieee80211_key *, uint8_t *);
static	int wep_encap(struct ieee80211_key *, struct mbuf *);
static	int wep_decap(struct ieee80211_key *, struct mbuf *, int);
static	int wep_enmic(struct ieee80211_key *, struct mbuf *, int);
static	int wep_demic(struct ieee80211_key *, struct mbuf *, int);

static const struct ieee80211_cipher wep = {
	.ic_name	= "WEP",
	.ic_cipher	= IEEE80211_CIPHER_WEP,
	.ic_header	= IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN,
	.ic_trailer	= IEEE80211_WEP_CRCLEN,
	.ic_miclen	= 0,
	.ic_attach	= wep_attach,
	.ic_detach	= wep_detach,
	.ic_setkey	= wep_setkey,
	.ic_setiv	= wep_setiv,
	.ic_encap	= wep_encap,
	.ic_decap	= wep_decap,
	.ic_enmic	= wep_enmic,
	.ic_demic	= wep_demic,
};

static	int wep_encrypt(struct ieee80211_key *, struct mbuf *, int hdrlen);
static	int wep_decrypt(struct ieee80211_key *, struct mbuf *, int hdrlen);

struct wep_ctx {
	struct ieee80211vap *wc_vap;	/* for diagnostics+statistics */
	struct ieee80211com *wc_ic;
	uint32_t	wc_iv;		/* initial vector for crypto */
};

/* number of references from net80211 layer */
static	int nrefs = 0;

static void *
wep_attach(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	struct wep_ctx *ctx;

	ctx = (struct wep_ctx *) IEEE80211_MALLOC(sizeof(struct wep_ctx),
		M_80211_CRYPTO, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ctx == NULL) {
		vap->iv_stats.is_crypto_nomem++;
		return NULL;
	}

	ctx->wc_vap = vap;
	ctx->wc_ic = vap->iv_ic;
	get_random_bytes(&ctx->wc_iv, sizeof(ctx->wc_iv));
	nrefs++;			/* NB: we assume caller locking */
	return ctx;
}

static void
wep_detach(struct ieee80211_key *k)
{
	struct wep_ctx *ctx = k->wk_private;

	IEEE80211_FREE(ctx, M_80211_CRYPTO);
	KASSERT(nrefs > 0, ("imbalanced attach/detach"));
	nrefs--;			/* NB: we assume caller locking */
}

static int
wep_setkey(struct ieee80211_key *k)
{
	return k->wk_keylen >= 40/NBBY;
}

static void
wep_setiv(struct ieee80211_key *k, uint8_t *ivp)
{
	struct wep_ctx *ctx = k->wk_private;
	struct ieee80211vap *vap = ctx->wc_vap;
	uint32_t iv;
	uint8_t keyid;

	keyid = ieee80211_crypto_get_keyid(vap, k) << 6;

	/*
	 * XXX
	 * IV must not duplicate during the lifetime of the key.
	 * But no mechanism to renew keys is defined in IEEE 802.11
	 * for WEP.  And the IV may be duplicated at other stations
	 * because the session key itself is shared.  So we use a
	 * pseudo random IV for now, though it is not the right way.
	 *
	 * NB: Rather than use a strictly random IV we select a
	 * random one to start and then increment the value for
	 * each frame.  This is an explicit tradeoff between
	 * overhead and security.  Given the basic insecurity of
	 * WEP this seems worthwhile.
	 */

	/*
	 * Skip 'bad' IVs from Fluhrer/Mantin/Shamir:
	 * (B, 255, N) with 3 <= B < 16 and 0 <= N <= 255
	 */
	iv = ctx->wc_iv;
	if ((iv & 0xff00) == 0xff00) {
		int B = (iv & 0xff0000) >> 16;
		if (3 <= B && B < 16)
			iv += 0x0100;
	}
	ctx->wc_iv = iv + 1;

	/*
	 * NB: Preserve byte order of IV for packet
	 *     sniffers; it doesn't matter otherwise.
	 */
#if _BYTE_ORDER == _BIG_ENDIAN
	ivp[0] = iv >> 0;
	ivp[1] = iv >> 8;
	ivp[2] = iv >> 16;
#else
	ivp[2] = iv >> 0;
	ivp[1] = iv >> 8;
	ivp[0] = iv >> 16;
#endif
	ivp[3] = keyid;
}

/*
 * Add privacy headers appropriate for the specified key.
 */
static int
wep_encap(struct ieee80211_key *k, struct mbuf *m)
{
	struct wep_ctx *ctx = k->wk_private;
	struct ieee80211com *ic = ctx->wc_ic;
	struct ieee80211_frame *wh;
	uint8_t *ivp;
	int hdrlen;
	int is_mgmt;

	hdrlen = ieee80211_hdrspace(ic, mtod(m, void *));
	wh = mtod(m, struct ieee80211_frame *);
	is_mgmt = IEEE80211_IS_MGMT(wh);

	/*
	 * Check to see if IV is required.
	 */
	if (is_mgmt && (k->wk_flags & IEEE80211_KEY_NOIVMGT))
		return 1;
	if ((! is_mgmt) && (k->wk_flags & IEEE80211_KEY_NOIV))
		return 1;

	/*
	 * Copy down 802.11 header and add the IV + KeyID.
	 */
	M_PREPEND(m, wep.ic_header, M_NOWAIT);
	if (m == NULL)
		return 0;
	ivp = mtod(m, uint8_t *);
	ovbcopy(ivp + wep.ic_header, ivp, hdrlen);
	ivp += hdrlen;

	wep_setiv(k, ivp);

	/*
	 * Finally, do software encrypt if needed.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWENCRYPT) &&
	    !wep_encrypt(k, m, hdrlen))
		return 0;

	return 1;
}

/*
 * Add MIC to the frame as needed.
 */
static int
wep_enmic(struct ieee80211_key *k, struct mbuf *m, int force)
{

	return 1;
}

/*
 * Validate and strip privacy headers (and trailer) for a
 * received frame.  If necessary, decrypt the frame using
 * the specified key.
 */
static int
wep_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	struct wep_ctx *ctx = k->wk_private;
	struct ieee80211vap *vap = ctx->wc_vap;
	struct ieee80211_frame *wh;
	const struct ieee80211_rx_stats *rxs;

	wh = mtod(m, struct ieee80211_frame *);

	rxs = ieee80211_get_rx_params_ptr(m);

	if ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_IV_STRIP))
		goto finish;

	/*
	 * Check if the device handled the decrypt in hardware.
	 * If so we just strip the header; otherwise we need to
	 * handle the decrypt in software.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWDECRYPT) &&
	    !wep_decrypt(k, m, hdrlen)) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
		    "%s", "WEP ICV mismatch on decrypt");
		vap->iv_stats.is_rx_wepfail++;
		return 0;
	}

	/*
	 * Copy up 802.11 header and strip crypto bits.
	 */
	ovbcopy(mtod(m, void *), mtod(m, uint8_t *) + wep.ic_header, hdrlen);
	m_adj(m, wep.ic_header);

finish:
	/* XXX TODO: do we have to strip this for offload devices? */
	m_adj(m, -wep.ic_trailer);

	return 1;
}

/*
 * Verify and strip MIC from the frame.
 */
static int
wep_demic(struct ieee80211_key *k, struct mbuf *skb, int force)
{
	return 1;
}

static const uint32_t crc32_table[256] = {
	0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
	0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
	0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
	0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
	0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
	0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
	0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
	0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
	0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
	0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
	0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
	0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
	0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
	0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
	0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
	0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
	0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
	0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
	0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
	0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
	0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
	0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
	0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
	0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
	0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
	0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
	0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
	0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
	0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
	0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
	0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
	0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
	0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
	0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
	0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
	0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
	0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
	0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
	0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
	0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
	0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
	0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
	0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
	0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
	0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
	0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
	0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
	0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
	0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
	0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
	0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
	0x2d02ef8dL
};

static int
wep_encrypt(struct ieee80211_key *key, struct mbuf *m0, int hdrlen)
{
#define S_SWAP(a,b) do { uint8_t t = S[a]; S[a] = S[b]; S[b] = t; } while(0)
	struct wep_ctx *ctx = key->wk_private;
	struct ieee80211vap *vap = ctx->wc_vap;
	struct mbuf *m = m0;
	uint8_t rc4key[IEEE80211_WEP_IVLEN + IEEE80211_KEYBUF_SIZE];
	uint8_t icv[IEEE80211_WEP_CRCLEN];
	uint32_t i, j, k, crc;
	size_t buflen, data_len;
	uint8_t S[256];
	uint8_t *pos;
	u_int off, keylen;

	vap->iv_stats.is_crypto_wep++;

	/* NB: this assumes the header was pulled up */
	memcpy(rc4key, mtod(m, uint8_t *) + hdrlen, IEEE80211_WEP_IVLEN);
	memcpy(rc4key + IEEE80211_WEP_IVLEN, key->wk_key, key->wk_keylen);

	/* Setup RC4 state */
	for (i = 0; i < 256; i++)
		S[i] = i;
	j = 0;
	keylen = key->wk_keylen + IEEE80211_WEP_IVLEN;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + rc4key[i % keylen]) & 0xff;
		S_SWAP(i, j);
	}

	off = hdrlen + wep.ic_header;
	data_len = m->m_pkthdr.len - off;

	/* Compute CRC32 over unencrypted data and apply RC4 to data */
	crc = ~0;
	i = j = 0;
	pos = mtod(m, uint8_t *) + off;
	buflen = m->m_len - off;
	for (;;) {
		if (buflen > data_len)
			buflen = data_len;
		data_len -= buflen;
		for (k = 0; k < buflen; k++) {
			crc = crc32_table[(crc ^ *pos) & 0xff] ^ (crc >> 8);
			i = (i + 1) & 0xff;
			j = (j + S[i]) & 0xff;
			S_SWAP(i, j);
			*pos++ ^= S[(S[i] + S[j]) & 0xff];
		}
		if (m->m_next == NULL) {
			if (data_len != 0) {		/* out of data */
				IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO,
				    ether_sprintf(mtod(m0,
					struct ieee80211_frame *)->i_addr2),
				    "out of data for WEP (data_len %zu)",
				    data_len);
				/* XXX stat */
				return 0;
			}
			break;
		}
		m = m->m_next;
		pos = mtod(m, uint8_t *);
		buflen = m->m_len;
	}
	crc = ~crc;

	/* Append little-endian CRC32 and encrypt it to produce ICV */
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;
	for (k = 0; k < IEEE80211_WEP_CRCLEN; k++) {
		i = (i + 1) & 0xff;
		j = (j + S[i]) & 0xff;
		S_SWAP(i, j);
		icv[k] ^= S[(S[i] + S[j]) & 0xff];
	}
	return m_append(m0, IEEE80211_WEP_CRCLEN, icv);
#undef S_SWAP
}

static int
wep_decrypt(struct ieee80211_key *key, struct mbuf *m0, int hdrlen)
{
#define S_SWAP(a,b) do { uint8_t t = S[a]; S[a] = S[b]; S[b] = t; } while(0)
	struct wep_ctx *ctx = key->wk_private;
	struct ieee80211vap *vap = ctx->wc_vap;
	struct mbuf *m = m0;
	uint8_t rc4key[IEEE80211_WEP_IVLEN + IEEE80211_KEYBUF_SIZE];
	uint8_t icv[IEEE80211_WEP_CRCLEN];
	uint32_t i, j, k, crc;
	size_t buflen, data_len;
	uint8_t S[256];
	uint8_t *pos;
	u_int off, keylen;

	vap->iv_stats.is_crypto_wep++;

	/* NB: this assumes the header was pulled up */
	memcpy(rc4key, mtod(m, uint8_t *) + hdrlen, IEEE80211_WEP_IVLEN);
	memcpy(rc4key + IEEE80211_WEP_IVLEN, key->wk_key, key->wk_keylen);

	/* Setup RC4 state */
	for (i = 0; i < 256; i++)
		S[i] = i;
	j = 0;
	keylen = key->wk_keylen + IEEE80211_WEP_IVLEN;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + rc4key[i % keylen]) & 0xff;
		S_SWAP(i, j);
	}

	off = hdrlen + wep.ic_header;
	data_len = m->m_pkthdr.len - (off + wep.ic_trailer);

	/* Compute CRC32 over unencrypted data and apply RC4 to data */
	crc = ~0;
	i = j = 0;
	pos = mtod(m, uint8_t *) + off;
	buflen = m->m_len - off;
	for (;;) {
		if (buflen > data_len)
			buflen = data_len;
		data_len -= buflen;
		for (k = 0; k < buflen; k++) {
			i = (i + 1) & 0xff;
			j = (j + S[i]) & 0xff;
			S_SWAP(i, j);
			*pos ^= S[(S[i] + S[j]) & 0xff];
			crc = crc32_table[(crc ^ *pos) & 0xff] ^ (crc >> 8);
			pos++;
		}
		m = m->m_next;
		if (m == NULL) {
			if (data_len != 0) {		/* out of data */
				IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO,
				    mtod(m0, struct ieee80211_frame *)->i_addr2,
				    "out of data for WEP (data_len %zu)",
				    data_len);
				return 0;
			}
			break;
		}
		pos = mtod(m, uint8_t *);
		buflen = m->m_len;
	}
	crc = ~crc;

	/* Encrypt little-endian CRC32 and verify that it matches with
	 * received ICV */
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;
	for (k = 0; k < IEEE80211_WEP_CRCLEN; k++) {
		i = (i + 1) & 0xff;
		j = (j + S[i]) & 0xff;
		S_SWAP(i, j);
		/* XXX assumes ICV is contiguous in mbuf */
		if ((icv[k] ^ S[(S[i] + S[j]) & 0xff]) != *pos++) {
			/* ICV mismatch - drop frame */
			return 0;
		}
	}
	return 1;
#undef S_SWAP
}

/*
 * Module glue.
 */
IEEE80211_CRYPTO_MODULE(wep, 1);
