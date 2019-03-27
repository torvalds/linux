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
 * IEEE 802.11i TKIP crypto support.
 *
 * Part of this module is derived from similar code in the Host
 * AP driver. The code is used with the consent of the author and
 * it's license is included below.
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

static	void *tkip_attach(struct ieee80211vap *, struct ieee80211_key *);
static	void tkip_detach(struct ieee80211_key *);
static	int tkip_setkey(struct ieee80211_key *);
static	void tkip_setiv(struct ieee80211_key *, uint8_t *);
static	int tkip_encap(struct ieee80211_key *, struct mbuf *);
static	int tkip_enmic(struct ieee80211_key *, struct mbuf *, int);
static	int tkip_decap(struct ieee80211_key *, struct mbuf *, int);
static	int tkip_demic(struct ieee80211_key *, struct mbuf *, int);

static const struct ieee80211_cipher tkip  = {
	.ic_name	= "TKIP",
	.ic_cipher	= IEEE80211_CIPHER_TKIP,
	.ic_header	= IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN +
			  IEEE80211_WEP_EXTIVLEN,
	.ic_trailer	= IEEE80211_WEP_CRCLEN,
	.ic_miclen	= IEEE80211_WEP_MICLEN,
	.ic_attach	= tkip_attach,
	.ic_detach	= tkip_detach,
	.ic_setkey	= tkip_setkey,
	.ic_setiv	= tkip_setiv,
	.ic_encap	= tkip_encap,
	.ic_decap	= tkip_decap,
	.ic_enmic	= tkip_enmic,
	.ic_demic	= tkip_demic,
};

typedef	uint8_t u8;
typedef	uint16_t u16;
typedef	uint32_t __u32;
typedef	uint32_t u32;

struct tkip_ctx {
	struct ieee80211vap *tc_vap;	/* for diagnostics+statistics */

	u16	tx_ttak[5];
	u8	tx_rc4key[16];		/* XXX for test module; make locals? */

	u16	rx_ttak[5];
	int	rx_phase1_done;
	u8	rx_rc4key[16];		/* XXX for test module; make locals? */
	uint64_t rx_rsc;		/* held until MIC verified */
};

static	void michael_mic(struct tkip_ctx *, const u8 *key,
		struct mbuf *m, u_int off, size_t data_len,
		u8 mic[IEEE80211_WEP_MICLEN]);
static	int tkip_encrypt(struct tkip_ctx *, struct ieee80211_key *,
		struct mbuf *, int hdr_len);
static	int tkip_decrypt(struct tkip_ctx *, struct ieee80211_key *,
		struct mbuf *, int hdr_len);

/* number of references from net80211 layer */
static	int nrefs = 0;

static void *
tkip_attach(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	struct tkip_ctx *ctx;

	ctx = (struct tkip_ctx *) IEEE80211_MALLOC(sizeof(struct tkip_ctx),
		M_80211_CRYPTO, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (ctx == NULL) {
		vap->iv_stats.is_crypto_nomem++;
		return NULL;
	}

	ctx->tc_vap = vap;
	nrefs++;			/* NB: we assume caller locking */
	return ctx;
}

static void
tkip_detach(struct ieee80211_key *k)
{
	struct tkip_ctx *ctx = k->wk_private;

	IEEE80211_FREE(ctx, M_80211_CRYPTO);
	KASSERT(nrefs > 0, ("imbalanced attach/detach"));
	nrefs--;			/* NB: we assume caller locking */
}

static int
tkip_setkey(struct ieee80211_key *k)
{
	struct tkip_ctx *ctx = k->wk_private;

	if (k->wk_keylen != (128/NBBY)) {
		(void) ctx;		/* XXX */
		IEEE80211_DPRINTF(ctx->tc_vap, IEEE80211_MSG_CRYPTO,
			"%s: Invalid key length %u, expecting %u\n",
			__func__, k->wk_keylen, 128/NBBY);
		return 0;
	}
	ctx->rx_phase1_done = 0;
	return 1;
}

static void
tkip_setiv(struct ieee80211_key *k, uint8_t *ivp)
{
	struct tkip_ctx *ctx = k->wk_private;
	struct ieee80211vap *vap = ctx->tc_vap;
	uint8_t keyid;

	keyid = ieee80211_crypto_get_keyid(vap, k) << 6;

	k->wk_keytsc++;
	ivp[0] = k->wk_keytsc >> 8;		/* TSC1 */
	ivp[1] = (ivp[0] | 0x20) & 0x7f;	/* WEP seed */
	ivp[2] = k->wk_keytsc >> 0;		/* TSC0 */
	ivp[3] = keyid | IEEE80211_WEP_EXTIV;	/* KeyID | ExtID */
	ivp[4] = k->wk_keytsc >> 16;		/* TSC2 */
	ivp[5] = k->wk_keytsc >> 24;		/* TSC3 */
	ivp[6] = k->wk_keytsc >> 32;		/* TSC4 */
	ivp[7] = k->wk_keytsc >> 40;		/* TSC5 */
}

/*
 * Add privacy headers and do any s/w encryption required.
 */
static int
tkip_encap(struct ieee80211_key *k, struct mbuf *m)
{
	struct tkip_ctx *ctx = k->wk_private;
	struct ieee80211vap *vap = ctx->tc_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_frame *wh;
	uint8_t *ivp;
	int hdrlen;
	int is_mgmt;

	wh = mtod(m, struct ieee80211_frame *);
	is_mgmt = IEEE80211_IS_MGMT(wh);

	/*
	 * Handle TKIP counter measures requirement.
	 */
	if (vap->iv_flags & IEEE80211_F_COUNTERM) {
#ifdef IEEE80211_DEBUG
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
#endif

		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
		    "discard frame due to countermeasures (%s)", __func__);
		vap->iv_stats.is_crypto_tkipcm++;
		return 0;
	}

	/*
	 * Check to see whether IV needs to be included.
	 */
	if (is_mgmt && (k->wk_flags & IEEE80211_KEY_NOIVMGT))
		return 1;
	if ((! is_mgmt) && (k->wk_flags & IEEE80211_KEY_NOIV))
		return 1;


	hdrlen = ieee80211_hdrspace(ic, mtod(m, void *));

	/*
	 * Copy down 802.11 header and add the IV, KeyID, and ExtIV.
	 */
	M_PREPEND(m, tkip.ic_header, M_NOWAIT);
	if (m == NULL)
		return 0;
	ivp = mtod(m, uint8_t *);
	memmove(ivp, ivp + tkip.ic_header, hdrlen);
	ivp += hdrlen;

	tkip_setiv(k, ivp);

	/*
	 * Finally, do software encrypt if needed.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWENCRYPT) &&
	    !tkip_encrypt(ctx, k, m, hdrlen))
		return 0;

	return 1;
}

/*
 * Add MIC to the frame as needed.
 */
static int
tkip_enmic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	struct tkip_ctx *ctx = k->wk_private;
	struct ieee80211_frame *wh;
	int is_mgmt;

	wh = mtod(m, struct ieee80211_frame *);
	is_mgmt = IEEE80211_IS_MGMT(wh);

	/*
	 * Check to see whether MIC needs to be included.
	 */
	if (is_mgmt && (k->wk_flags & IEEE80211_KEY_NOMICMGT))
		return 1;
	if ((! is_mgmt) && (k->wk_flags & IEEE80211_KEY_NOMIC))
		return 1;

	if (force || (k->wk_flags & IEEE80211_KEY_SWENMIC)) {
		struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
		struct ieee80211vap *vap = ctx->tc_vap;
		struct ieee80211com *ic = vap->iv_ic;
		int hdrlen;
		uint8_t mic[IEEE80211_WEP_MICLEN];

		vap->iv_stats.is_crypto_tkipenmic++;

		hdrlen = ieee80211_hdrspace(ic, wh);

		michael_mic(ctx, k->wk_txmic,
			m, hdrlen, m->m_pkthdr.len - hdrlen, mic);
		return m_append(m, tkip.ic_miclen, mic);
	}
	return 1;
}

static __inline uint64_t
READ_6(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
	uint32_t iv32 = (b0 << 0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
	uint16_t iv16 = (b4 << 0) | (b5 << 8);
	return (((uint64_t)iv16) << 32) | iv32;
}

/*
 * Validate and strip privacy headers (and trailer) for a
 * received frame.  If necessary, decrypt the frame using
 * the specified key.
 */
static int
tkip_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	const struct ieee80211_rx_stats *rxs;
	struct tkip_ctx *ctx = k->wk_private;
	struct ieee80211vap *vap = ctx->tc_vap;
	struct ieee80211_frame *wh;
	uint8_t *ivp, tid;

	rxs = ieee80211_get_rx_params_ptr(m);

	/*
	 * If IV has been stripped, we skip most of the below.
	 */
	if ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_IV_STRIP))
		goto finish;

	/*
	 * Header should have extended IV and sequence number;
	 * verify the former and validate the latter.
	 */
	wh = mtod(m, struct ieee80211_frame *);
	ivp = mtod(m, uint8_t *) + hdrlen;
	if ((ivp[IEEE80211_WEP_IVLEN] & IEEE80211_WEP_EXTIV) == 0) {
		/*
		 * No extended IV; discard frame.
		 */
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
		    "%s", "missing ExtIV for TKIP cipher");
		vap->iv_stats.is_rx_tkipformat++;
		return 0;
	}
	/*
	 * Handle TKIP counter measures requirement.
	 */
	if (vap->iv_flags & IEEE80211_F_COUNTERM) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
		    "discard frame due to countermeasures (%s)", __func__);
		vap->iv_stats.is_crypto_tkipcm++;
		return 0;
	}

	tid = ieee80211_gettid(wh);
	ctx->rx_rsc = READ_6(ivp[2], ivp[0], ivp[4], ivp[5], ivp[6], ivp[7]);
	if (ctx->rx_rsc <= k->wk_keyrsc[tid] &&
	    (k->wk_flags & IEEE80211_KEY_NOREPLAY) == 0) {
		/*
		 * Replay violation; notify upper layer.
		 */
		ieee80211_notify_replay_failure(vap, wh, k, ctx->rx_rsc, tid);
		vap->iv_stats.is_rx_tkipreplay++;
		return 0;
	}
	/*
	 * NB: We can't update the rsc in the key until MIC is verified.
	 *
	 * We assume we are not preempted between doing the check above
	 * and updating wk_keyrsc when stripping the MIC in tkip_demic.
	 * Otherwise we might process another packet and discard it as
	 * a replay.
	 */

	/*
	 * Check if the device handled the decrypt in hardware.
	 * If so we just strip the header; otherwise we need to
	 * handle the decrypt in software.
	 */
	if ((k->wk_flags & IEEE80211_KEY_SWDECRYPT) &&
	    !tkip_decrypt(ctx, k, m, hdrlen))
		return 0;

finish:

	/*
	 * Copy up 802.11 header and strip crypto bits - but only if we
	 * are required to.
	 */
	if (! ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_IV_STRIP))) {
		memmove(mtod(m, uint8_t *) + tkip.ic_header, mtod(m, void *),
		    hdrlen);
		m_adj(m, tkip.ic_header);
	}

	/*
	 * XXX TODO: do we need an option to potentially not strip the
	 * WEP trailer?  Does "MMIC_STRIP" also mean this? Or?
	 */
	m_adj(m, -tkip.ic_trailer);

	return 1;
}

/*
 * Verify and strip MIC from the frame.
 */
static int
tkip_demic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	const struct ieee80211_rx_stats *rxs;
	struct tkip_ctx *ctx = k->wk_private;
	struct ieee80211_frame *wh;
	uint8_t tid;

	wh = mtod(m, struct ieee80211_frame *);
	rxs = ieee80211_get_rx_params_ptr(m);

	/*
	 * If we are told about a MIC failure from the driver,
	 * directly notify as a michael failure to the upper
	 * layers.
	 */
	if ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_FAIL_MIC)) {
		struct ieee80211vap *vap = ctx->tc_vap;
		ieee80211_notify_michael_failure(vap, wh,
		    k->wk_rxkeyix != IEEE80211_KEYIX_NONE ?
		    k->wk_rxkeyix : k->wk_keyix);
		return 0;
	}

	/*
	 * If IV has been stripped, we skip most of the below.
	 */
	if ((rxs != NULL) && (rxs->c_pktflags & IEEE80211_RX_F_MMIC_STRIP))
		goto finish;

	if ((k->wk_flags & IEEE80211_KEY_SWDEMIC) || force) {
		struct ieee80211vap *vap = ctx->tc_vap;
		int hdrlen = ieee80211_hdrspace(vap->iv_ic, wh);
		u8 mic[IEEE80211_WEP_MICLEN];
		u8 mic0[IEEE80211_WEP_MICLEN];

		vap->iv_stats.is_crypto_tkipdemic++;

		michael_mic(ctx, k->wk_rxmic, 
			m, hdrlen, m->m_pkthdr.len - (hdrlen + tkip.ic_miclen),
			mic);
		m_copydata(m, m->m_pkthdr.len - tkip.ic_miclen,
			tkip.ic_miclen, mic0);
		if (memcmp(mic, mic0, tkip.ic_miclen)) {
			/* NB: 802.11 layer handles statistic and debug msg */
			ieee80211_notify_michael_failure(vap, wh,
				k->wk_rxkeyix != IEEE80211_KEYIX_NONE ?
					k->wk_rxkeyix : k->wk_keyix);
			return 0;
		}
	}
	/*
	 * Strip MIC from the tail.
	 */
	m_adj(m, -tkip.ic_miclen);

	/*
	 * Ok to update rsc now that MIC has been verified.
	 */
	tid = ieee80211_gettid(wh);
	k->wk_keyrsc[tid] = ctx->rx_rsc;

finish:
	return 1;
}

/*
 * Host AP crypt: host-based TKIP encryption implementation for Host AP driver
 *
 * Copyright (c) 2003-2004, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 */

static const __u32 crc32_table[256] = {
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

static __inline u16 RotR1(u16 val)
{
	return (val >> 1) | (val << 15);
}

static __inline u8 Lo8(u16 val)
{
	return val & 0xff;
}

static __inline u8 Hi8(u16 val)
{
	return val >> 8;
}

static __inline u16 Lo16(u32 val)
{
	return val & 0xffff;
}

static __inline u16 Hi16(u32 val)
{
	return val >> 16;
}

static __inline u16 Mk16(u8 hi, u8 lo)
{
	return lo | (((u16) hi) << 8);
}

static __inline u16 Mk16_le(const u16 *v)
{
	return le16toh(*v);
}

static const u16 Sbox[256] = {
	0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
	0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
	0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
	0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
	0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
	0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
	0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
	0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
	0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
	0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
	0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
	0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
	0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
	0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
	0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
	0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
	0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
	0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
	0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
	0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
	0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
	0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
	0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
	0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
	0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
	0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
	0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
	0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
	0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
	0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
	0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
	0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A,
};

static __inline u16 _S_(u16 v)
{
	u16 t = Sbox[Hi8(v)];
	return Sbox[Lo8(v)] ^ ((t << 8) | (t >> 8));
}

#define PHASE1_LOOP_COUNT 8

static void tkip_mixing_phase1(u16 *TTAK, const u8 *TK, const u8 *TA, u32 IV32)
{
	int i, j;

	/* Initialize the 80-bit TTAK from TSC (IV32) and TA[0..5] */
	TTAK[0] = Lo16(IV32);
	TTAK[1] = Hi16(IV32);
	TTAK[2] = Mk16(TA[1], TA[0]);
	TTAK[3] = Mk16(TA[3], TA[2]);
	TTAK[4] = Mk16(TA[5], TA[4]);

	for (i = 0; i < PHASE1_LOOP_COUNT; i++) {
		j = 2 * (i & 1);
		TTAK[0] += _S_(TTAK[4] ^ Mk16(TK[1 + j], TK[0 + j]));
		TTAK[1] += _S_(TTAK[0] ^ Mk16(TK[5 + j], TK[4 + j]));
		TTAK[2] += _S_(TTAK[1] ^ Mk16(TK[9 + j], TK[8 + j]));
		TTAK[3] += _S_(TTAK[2] ^ Mk16(TK[13 + j], TK[12 + j]));
		TTAK[4] += _S_(TTAK[3] ^ Mk16(TK[1 + j], TK[0 + j])) + i;
	}
}

#ifndef _BYTE_ORDER
#error "Don't know native byte order"
#endif

static void tkip_mixing_phase2(u8 *WEPSeed, const u8 *TK, const u16 *TTAK,
			       u16 IV16)
{
	/* Make temporary area overlap WEP seed so that the final copy can be
	 * avoided on little endian hosts. */
	u16 *PPK = (u16 *) &WEPSeed[4];

	/* Step 1 - make copy of TTAK and bring in TSC */
	PPK[0] = TTAK[0];
	PPK[1] = TTAK[1];
	PPK[2] = TTAK[2];
	PPK[3] = TTAK[3];
	PPK[4] = TTAK[4];
	PPK[5] = TTAK[4] + IV16;

	/* Step 2 - 96-bit bijective mixing using S-box */
	PPK[0] += _S_(PPK[5] ^ Mk16_le((const u16 *) &TK[0]));
	PPK[1] += _S_(PPK[0] ^ Mk16_le((const u16 *) &TK[2]));
	PPK[2] += _S_(PPK[1] ^ Mk16_le((const u16 *) &TK[4]));
	PPK[3] += _S_(PPK[2] ^ Mk16_le((const u16 *) &TK[6]));
	PPK[4] += _S_(PPK[3] ^ Mk16_le((const u16 *) &TK[8]));
	PPK[5] += _S_(PPK[4] ^ Mk16_le((const u16 *) &TK[10]));

	PPK[0] += RotR1(PPK[5] ^ Mk16_le((const u16 *) &TK[12]));
	PPK[1] += RotR1(PPK[0] ^ Mk16_le((const u16 *) &TK[14]));
	PPK[2] += RotR1(PPK[1]);
	PPK[3] += RotR1(PPK[2]);
	PPK[4] += RotR1(PPK[3]);
	PPK[5] += RotR1(PPK[4]);

	/* Step 3 - bring in last of TK bits, assign 24-bit WEP IV value
	 * WEPSeed[0..2] is transmitted as WEP IV */
	WEPSeed[0] = Hi8(IV16);
	WEPSeed[1] = (Hi8(IV16) | 0x20) & 0x7F;
	WEPSeed[2] = Lo8(IV16);
	WEPSeed[3] = Lo8((PPK[5] ^ Mk16_le((const u16 *) &TK[0])) >> 1);

#if _BYTE_ORDER == _BIG_ENDIAN
	{
		int i;
		for (i = 0; i < 6; i++)
			PPK[i] = (PPK[i] << 8) | (PPK[i] >> 8);
	}
#endif
}

static void
wep_encrypt(u8 *key, struct mbuf *m0, u_int off, size_t data_len,
	uint8_t icv[IEEE80211_WEP_CRCLEN])
{
	u32 i, j, k, crc;
	size_t buflen;
	u8 S[256];
	u8 *pos;
	struct mbuf *m;
#define S_SWAP(a,b) do { u8 t = S[a]; S[a] = S[b]; S[b] = t; } while(0)

	/* Setup RC4 state */
	for (i = 0; i < 256; i++)
		S[i] = i;
	j = 0;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + key[i & 0x0f]) & 0xff;
		S_SWAP(i, j);
	}

	/* Compute CRC32 over unencrypted data and apply RC4 to data */
	crc = ~0;
	i = j = 0;
	m = m0;
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
		m = m->m_next;
		if (m == NULL) {
			KASSERT(data_len == 0,
			    ("out of buffers with data_len %zu\n", data_len));
			break;
		}
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
}

static int
wep_decrypt(u8 *key, struct mbuf *m, u_int off, size_t data_len)
{
	u32 i, j, k, crc;
	u8 S[256];
	u8 *pos, icv[4];
	size_t buflen;

	/* Setup RC4 state */
	for (i = 0; i < 256; i++)
		S[i] = i;
	j = 0;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + key[i & 0x0f]) & 0xff;
		S_SWAP(i, j);
	}

	/* Apply RC4 to data and compute CRC32 over decrypted data */
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
			KASSERT(data_len == 0,
			    ("out of buffers with data_len %zu\n", data_len));
			break;
		}
		pos = mtod(m, uint8_t *);
		buflen = m->m_len;
	}
	crc = ~crc;

	/* Encrypt little-endian CRC32 and verify that it matches with the
	 * received ICV */
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;
	for (k = 0; k < 4; k++) {
		i = (i + 1) & 0xff;
		j = (j + S[i]) & 0xff;
		S_SWAP(i, j);
		if ((icv[k] ^ S[(S[i] + S[j]) & 0xff]) != *pos++) {
			/* ICV mismatch - drop frame */
			return -1;
		}
	}

	return 0;
}


static __inline u32 rotl(u32 val, int bits)
{
	return (val << bits) | (val >> (32 - bits));
}


static __inline u32 rotr(u32 val, int bits)
{
	return (val >> bits) | (val << (32 - bits));
}


static __inline u32 xswap(u32 val)
{
	return ((val & 0x00ff00ff) << 8) | ((val & 0xff00ff00) >> 8);
}


#define michael_block(l, r)	\
do {				\
	r ^= rotl(l, 17);	\
	l += r;			\
	r ^= xswap(l);		\
	l += r;			\
	r ^= rotl(l, 3);	\
	l += r;			\
	r ^= rotr(l, 2);	\
	l += r;			\
} while (0)


static __inline u32 get_le32_split(u8 b0, u8 b1, u8 b2, u8 b3)
{
	return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static __inline u32 get_le32(const u8 *p)
{
	return get_le32_split(p[0], p[1], p[2], p[3]);
}


static __inline void put_le32(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

/*
 * Craft pseudo header used to calculate the MIC.
 */
static void
michael_mic_hdr(const struct ieee80211_frame *wh0, uint8_t hdr[16])
{
	const struct ieee80211_frame_addr4 *wh =
		(const struct ieee80211_frame_addr4 *) wh0;

	switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		IEEE80211_ADDR_COPY(hdr, wh->i_addr1); /* DA */
		IEEE80211_ADDR_COPY(hdr + IEEE80211_ADDR_LEN, wh->i_addr2);
		break;
	case IEEE80211_FC1_DIR_TODS:
		IEEE80211_ADDR_COPY(hdr, wh->i_addr3); /* DA */
		IEEE80211_ADDR_COPY(hdr + IEEE80211_ADDR_LEN, wh->i_addr2);
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		IEEE80211_ADDR_COPY(hdr, wh->i_addr1); /* DA */
		IEEE80211_ADDR_COPY(hdr + IEEE80211_ADDR_LEN, wh->i_addr3);
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		IEEE80211_ADDR_COPY(hdr, wh->i_addr3); /* DA */
		IEEE80211_ADDR_COPY(hdr + IEEE80211_ADDR_LEN, wh->i_addr4);
		break;
	}

	if (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_QOS) {
		const struct ieee80211_qosframe *qwh =
			(const struct ieee80211_qosframe *) wh;
		hdr[12] = qwh->i_qos[0] & IEEE80211_QOS_TID;
	} else
		hdr[12] = 0;
	hdr[13] = hdr[14] = hdr[15] = 0; /* reserved */
}

static void
michael_mic(struct tkip_ctx *ctx, const u8 *key,
	struct mbuf *m, u_int off, size_t data_len,
	u8 mic[IEEE80211_WEP_MICLEN])
{
	uint8_t hdr[16];
	u32 l, r;
	const uint8_t *data;
	u_int space;

	michael_mic_hdr(mtod(m, struct ieee80211_frame *), hdr);

	l = get_le32(key);
	r = get_le32(key + 4);

	/* Michael MIC pseudo header: DA, SA, 3 x 0, Priority */
	l ^= get_le32(hdr);
	michael_block(l, r);
	l ^= get_le32(&hdr[4]);
	michael_block(l, r);
	l ^= get_le32(&hdr[8]);
	michael_block(l, r);
	l ^= get_le32(&hdr[12]);
	michael_block(l, r);

	/* first buffer has special handling */
	data = mtod(m, const uint8_t *) + off;
	space = m->m_len - off;
	for (;;) {
		if (space > data_len)
			space = data_len;
		/* collect 32-bit blocks from current buffer */
		while (space >= sizeof(uint32_t)) {
			l ^= get_le32(data);
			michael_block(l, r);
			data += sizeof(uint32_t), space -= sizeof(uint32_t);
			data_len -= sizeof(uint32_t);
		}
		/*
		 * NB: when space is zero we make one more trip around
		 * the loop to advance to the next mbuf where there is
		 * data.  This handles the case where there are 4*n
		 * bytes in an mbuf followed by <4 bytes in a later mbuf.
		 * By making an extra trip we'll drop out of the loop
		 * with m pointing at the mbuf with 3 bytes and space
		 * set as required by the remainder handling below.
		 */
		if (data_len == 0 ||
		    (data_len < sizeof(uint32_t) && space != 0))
			break;
		m = m->m_next;
		if (m == NULL) {
			KASSERT(0, ("out of data, data_len %zu\n", data_len));
			break;
		}
		if (space != 0) {
			const uint8_t *data_next;
			/*
			 * Block straddles buffers, split references.
			 */
			data_next = mtod(m, const uint8_t *);
			KASSERT(m->m_len >= sizeof(uint32_t) - space,
				("not enough data in following buffer, "
				"m_len %u need %zu\n", m->m_len,
				sizeof(uint32_t) - space));
			switch (space) {
			case 1:
				l ^= get_le32_split(data[0], data_next[0],
					data_next[1], data_next[2]);
				data = data_next + 3;
				space = m->m_len - 3;
				break;
			case 2:
				l ^= get_le32_split(data[0], data[1],
					data_next[0], data_next[1]);
				data = data_next + 2;
				space = m->m_len - 2;
				break;
			case 3:
				l ^= get_le32_split(data[0], data[1],
					data[2], data_next[0]);
				data = data_next + 1;
				space = m->m_len - 1;
				break;
			}
			michael_block(l, r);
			data_len -= sizeof(uint32_t);
		} else {
			/*
			 * Setup for next buffer.
			 */
			data = mtod(m, const uint8_t *);
			space = m->m_len;
		}
	}
	/*
	 * Catch degenerate cases like mbuf[4*n+1 bytes] followed by
	 * mbuf[2 bytes].  I don't believe these should happen; if they
	 * do then we'll need more involved logic.
	 */
	KASSERT(data_len <= space,
	    ("not enough data, data_len %zu space %u\n", data_len, space));

	/* Last block and padding (0x5a, 4..7 x 0) */
	switch (data_len) {
	case 0:
		l ^= get_le32_split(0x5a, 0, 0, 0);
		break;
	case 1:
		l ^= get_le32_split(data[0], 0x5a, 0, 0);
		break;
	case 2:
		l ^= get_le32_split(data[0], data[1], 0x5a, 0);
		break;
	case 3:
		l ^= get_le32_split(data[0], data[1], data[2], 0x5a);
		break;
	}
	michael_block(l, r);
	/* l ^= 0; */
	michael_block(l, r);

	put_le32(mic, l);
	put_le32(mic + 4, r);
}

static int
tkip_encrypt(struct tkip_ctx *ctx, struct ieee80211_key *key,
	struct mbuf *m, int hdrlen)
{
	struct ieee80211_frame *wh;
	uint8_t icv[IEEE80211_WEP_CRCLEN];

	ctx->tc_vap->iv_stats.is_crypto_tkip++;

	wh = mtod(m, struct ieee80211_frame *);
	if ((u16)(key->wk_keytsc) == 0 || key->wk_keytsc == 1) {
		tkip_mixing_phase1(ctx->tx_ttak, key->wk_key, wh->i_addr2,
				   (u32)(key->wk_keytsc >> 16));
	}
	tkip_mixing_phase2(ctx->tx_rc4key, key->wk_key, ctx->tx_ttak,
		(u16) key->wk_keytsc);

	wep_encrypt(ctx->tx_rc4key,
		m, hdrlen + tkip.ic_header,
		m->m_pkthdr.len - (hdrlen + tkip.ic_header),
		icv);
	(void) m_append(m, IEEE80211_WEP_CRCLEN, icv);	/* XXX check return */

	return 1;
}

static int
tkip_decrypt(struct tkip_ctx *ctx, struct ieee80211_key *key,
	struct mbuf *m, int hdrlen)
{
	struct ieee80211_frame *wh;
	struct ieee80211vap *vap = ctx->tc_vap;
	u32 iv32;
	u16 iv16;
	u8 tid;

	vap->iv_stats.is_crypto_tkip++;

	wh = mtod(m, struct ieee80211_frame *);
	/* NB: tkip_decap already verified header and left seq in rx_rsc */
	iv16 = (u16) ctx->rx_rsc;
	iv32 = (u32) (ctx->rx_rsc >> 16);

	tid = ieee80211_gettid(wh);
	if (iv32 != (u32)(key->wk_keyrsc[tid] >> 16) || !ctx->rx_phase1_done) {
		tkip_mixing_phase1(ctx->rx_ttak, key->wk_key,
			wh->i_addr2, iv32);
		ctx->rx_phase1_done = 1;
	}
	tkip_mixing_phase2(ctx->rx_rc4key, key->wk_key, ctx->rx_ttak, iv16);

	/* NB: m is unstripped; deduct headers + ICV to get payload */
	if (wep_decrypt(ctx->rx_rc4key,
		m, hdrlen + tkip.ic_header,
	        m->m_pkthdr.len - (hdrlen + tkip.ic_header + tkip.ic_trailer))) {
		if (iv32 != (u32)(key->wk_keyrsc[tid] >> 16)) {
			/* Previously cached Phase1 result was already lost, so
			 * it needs to be recalculated for the next packet. */
			ctx->rx_phase1_done = 0;
		}
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
		    "%s", "TKIP ICV mismatch on decrypt");
		vap->iv_stats.is_rx_tkipicv++;
		return 0;
	}
	return 1;
}

/*
 * Module glue.
 */
IEEE80211_CRYPTO_MODULE(tkip, 1);
