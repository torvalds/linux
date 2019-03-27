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
 * IEEE 802.11 NULL crypto support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h> 
#include <sys/malloc.h> 
#include <sys/systm.h> 
#include <sys/mbuf.h>   
#include <sys/module.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>

static	void *none_attach(struct ieee80211vap *, struct ieee80211_key *);
static	void none_detach(struct ieee80211_key *);
static	int none_setkey(struct ieee80211_key *);
static	void none_setiv(struct ieee80211_key *, uint8_t *);
static	int none_encap(struct ieee80211_key *, struct mbuf *);
static	int none_decap(struct ieee80211_key *, struct mbuf *, int);
static	int none_enmic(struct ieee80211_key *, struct mbuf *, int);
static	int none_demic(struct ieee80211_key *, struct mbuf *, int);

const struct ieee80211_cipher ieee80211_cipher_none = {
	.ic_name	= "NONE",
	.ic_cipher	= IEEE80211_CIPHER_NONE,
	.ic_header	= 0,
	.ic_trailer	= 0,
	.ic_miclen	= 0,
	.ic_attach	= none_attach,
	.ic_detach	= none_detach,
	.ic_setkey	= none_setkey,
	.ic_setiv	= none_setiv,
	.ic_encap	= none_encap,
	.ic_decap	= none_decap,
	.ic_enmic	= none_enmic,
	.ic_demic	= none_demic,
};

static void *
none_attach(struct ieee80211vap *vap, struct ieee80211_key *k)
{
	return vap;		/* for diagnostics+stats */
}

static void
none_detach(struct ieee80211_key *k)
{
	(void) k;
}

static int
none_setkey(struct ieee80211_key *k)
{
	(void) k;
	return 1;
}

static void
none_setiv(struct ieee80211_key *k, uint8_t *ivp)
{
}

static int
none_encap(struct ieee80211_key *k, struct mbuf *m)
{
	struct ieee80211vap *vap = k->wk_private;
#ifdef IEEE80211_DEBUG
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	uint8_t keyid;

	keyid = ieee80211_crypto_get_keyid(vap, k);

	/*
	 * The specified key is not setup; this can
	 * happen, at least, when changing keys.
	 */
	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr1,
	    "key id %u is not set (encap)", keyid);
#endif
	vap->iv_stats.is_tx_badcipher++;
	return 0;
}

static int
none_decap(struct ieee80211_key *k, struct mbuf *m, int hdrlen)
{
	struct ieee80211vap *vap = k->wk_private;
#ifdef IEEE80211_DEBUG
	struct ieee80211_frame *wh = mtod(m, struct ieee80211_frame *);
	const uint8_t *ivp = (const uint8_t *)&wh[1];
#endif

	/*
	 * The specified key is not setup; this can
	 * happen, at least, when changing keys.
	 */
	/* XXX useful to know dst too */
	IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_CRYPTO, wh->i_addr2,
	    "key id %u is not set (decap)", ivp[IEEE80211_WEP_IVLEN] >> 6);
	vap->iv_stats.is_rx_badkeyid++;
	return 0;
}

static int
none_enmic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	struct ieee80211vap *vap = k->wk_private;

	vap->iv_stats.is_tx_badcipher++;
	return 0;
}

static int
none_demic(struct ieee80211_key *k, struct mbuf *m, int force)
{
	struct ieee80211vap *vap = k->wk_private;

	vap->iv_stats.is_rx_badkeyid++;
	return 0;
}
