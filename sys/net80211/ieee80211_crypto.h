/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_CRYPTO_H_
#define _NET80211_IEEE80211_CRYPTO_H_

/*
 * 802.11 protocol crypto-related definitions.
 */
#define	IEEE80211_KEYBUF_SIZE	16
#define	IEEE80211_MICBUF_SIZE	(8+8)	/* space for both tx+rx keys */

/*
 * Old WEP-style key.  Deprecated.
 */
struct ieee80211_wepkey {
	u_int		wk_len;		/* key length in bytes */
	uint8_t		wk_key[IEEE80211_KEYBUF_SIZE];
};

struct ieee80211_rsnparms {
	uint8_t		rsn_mcastcipher;	/* mcast/group cipher */
	uint8_t		rsn_mcastkeylen;	/* mcast key length */
	uint8_t		rsn_ucastcipher;	/* selected unicast cipher */
	uint8_t		rsn_ucastkeylen;	/* unicast key length */
	uint8_t		rsn_keymgmt;		/* selected key mgmt algo */
	uint16_t	rsn_caps;		/* capabilities */
};

struct ieee80211_cipher;

/*
 * Crypto key state.  There is sufficient room for all supported
 * ciphers (see below).  The underlying ciphers are handled
 * separately through loadable cipher modules that register with
 * the generic crypto support.  A key has a reference to an instance
 * of the cipher; any per-key state is hung off wk_private by the
 * cipher when it is attached.  Ciphers are automatically called
 * to detach and cleanup any such state when the key is deleted.
 *
 * The generic crypto support handles encap/decap of cipher-related
 * frame contents for both hardware- and software-based implementations.
 * A key requiring software crypto support is automatically flagged and
 * the cipher is expected to honor this and do the necessary work.
 * Ciphers such as TKIP may also support mixed hardware/software
 * encrypt/decrypt and MIC processing.
 */
typedef uint16_t ieee80211_keyix;	/* h/w key index */

struct ieee80211_key {
	uint8_t		wk_keylen;	/* key length in bytes */
	uint8_t		wk_pad;		/* .. some drivers use this. Fix that. */
	uint8_t		wk_pad1[2];
	uint32_t	wk_flags;
#define	IEEE80211_KEY_XMIT	0x00000001	/* key used for xmit */
#define	IEEE80211_KEY_RECV	0x00000002	/* key used for recv */
#define	IEEE80211_KEY_GROUP	0x00000004	/* key used for WPA group operation */
#define	IEEE80211_KEY_NOREPLAY	0x00000008	/* ignore replay failures */
#define	IEEE80211_KEY_SWENCRYPT	0x00000010	/* host-based encrypt */
#define	IEEE80211_KEY_SWDECRYPT	0x00000020	/* host-based decrypt */
#define	IEEE80211_KEY_SWENMIC	0x00000040	/* host-based enmic */
#define	IEEE80211_KEY_SWDEMIC	0x00000080	/* host-based demic */
#define	IEEE80211_KEY_DEVKEY	0x00000100	/* device key request completed */
#define	IEEE80211_KEY_CIPHER0	0x00001000	/* cipher-specific action 0 */
#define	IEEE80211_KEY_CIPHER1	0x00002000	/* cipher-specific action 1 */
#define	IEEE80211_KEY_NOIV	0x00004000	/* don't insert IV/MIC for !mgmt */
#define	IEEE80211_KEY_NOIVMGT	0x00008000	/* don't insert IV/MIC for mgmt */
#define	IEEE80211_KEY_NOMIC	0x00010000	/* don't insert MIC for !mgmt */
#define	IEEE80211_KEY_NOMICMGT	0x00020000	/* don't insert MIC for mgmt */

	ieee80211_keyix	wk_keyix;	/* h/w key index */
	ieee80211_keyix	wk_rxkeyix;	/* optional h/w rx key index */
	uint8_t		wk_key[IEEE80211_KEYBUF_SIZE+IEEE80211_MICBUF_SIZE];
#define	wk_txmic	wk_key+IEEE80211_KEYBUF_SIZE+0	/* XXX can't () right */
#define	wk_rxmic	wk_key+IEEE80211_KEYBUF_SIZE+8	/* XXX can't () right */
					/* key receive sequence counter */
	uint64_t	wk_keyrsc[IEEE80211_TID_SIZE];
	uint64_t	wk_keytsc;	/* key transmit sequence counter */
	const struct ieee80211_cipher *wk_cipher;
	void		*wk_private;	/* private cipher state */
	uint8_t		wk_macaddr[IEEE80211_ADDR_LEN];
};
#define	IEEE80211_KEY_COMMON 		/* common flags passed in by apps */\
	(IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV | IEEE80211_KEY_GROUP | \
	 IEEE80211_KEY_NOREPLAY)

#define	IEEE80211_KEY_SWCRYPT \
	(IEEE80211_KEY_SWENCRYPT | IEEE80211_KEY_SWDECRYPT)
#define	IEEE80211_KEY_SWMIC	(IEEE80211_KEY_SWENMIC | IEEE80211_KEY_SWDEMIC)

#define IEEE80211_KEY_DEVICE		/* flags owned by device driver */\
	(IEEE80211_KEY_DEVKEY|IEEE80211_KEY_CIPHER0|IEEE80211_KEY_CIPHER1| \
	 IEEE80211_KEY_SWCRYPT|IEEE80211_KEY_SWMIC|IEEE80211_KEY_NOIV | \
	 IEEE80211_KEY_NOIVMGT|IEEE80211_KEY_NOMIC|IEEE80211_KEY_NOMICMGT)

#define	IEEE80211_KEY_BITS \
	"\20\1XMIT\2RECV\3GROUP\4SWENCRYPT\5SWDECRYPT\6SWENMIC\7SWDEMIC" \
	"\10DEVKEY\11CIPHER0\12CIPHER1"

#define	IEEE80211_KEYIX_NONE	((ieee80211_keyix) -1)

/*
 * NB: these values are ordered carefully; there are lots of
 * of implications in any reordering.  Beware that 4 is used
 * only to indicate h/w TKIP MIC support in driver capabilities;
 * there is no separate cipher support (it's rolled into the
 * TKIP cipher support).
 */
#define	IEEE80211_CIPHER_WEP		0
#define	IEEE80211_CIPHER_TKIP		1
#define	IEEE80211_CIPHER_AES_OCB	2
#define	IEEE80211_CIPHER_AES_CCM	3
#define	IEEE80211_CIPHER_TKIPMIC	4	/* TKIP MIC capability */
#define	IEEE80211_CIPHER_CKIP		5
#define	IEEE80211_CIPHER_NONE		6	/* pseudo value */

#define	IEEE80211_CIPHER_MAX		(IEEE80211_CIPHER_NONE+1)

/* capability bits in ic_cryptocaps/iv_cryptocaps */
#define	IEEE80211_CRYPTO_WEP		(1<<IEEE80211_CIPHER_WEP)
#define	IEEE80211_CRYPTO_TKIP		(1<<IEEE80211_CIPHER_TKIP)
#define	IEEE80211_CRYPTO_AES_OCB	(1<<IEEE80211_CIPHER_AES_OCB)
#define	IEEE80211_CRYPTO_AES_CCM	(1<<IEEE80211_CIPHER_AES_CCM)
#define	IEEE80211_CRYPTO_TKIPMIC	(1<<IEEE80211_CIPHER_TKIPMIC)
#define	IEEE80211_CRYPTO_CKIP		(1<<IEEE80211_CIPHER_CKIP)

#define	IEEE80211_CRYPTO_BITS \
	"\20\1WEP\2TKIP\3AES\4AES_CCM\5TKIPMIC\6CKIP"

#if defined(__KERNEL__) || defined(_KERNEL)

struct ieee80211com;
struct ieee80211vap;
struct ieee80211_node;
struct mbuf;

MALLOC_DECLARE(M_80211_CRYPTO);

void	ieee80211_crypto_attach(struct ieee80211com *);
void	ieee80211_crypto_detach(struct ieee80211com *);
void	ieee80211_crypto_vattach(struct ieee80211vap *);
void	ieee80211_crypto_vdetach(struct ieee80211vap *);
int	ieee80211_crypto_newkey(struct ieee80211vap *,
		int cipher, int flags, struct ieee80211_key *);
int	ieee80211_crypto_delkey(struct ieee80211vap *,
		struct ieee80211_key *);
int	ieee80211_crypto_setkey(struct ieee80211vap *, struct ieee80211_key *);
void	ieee80211_crypto_delglobalkeys(struct ieee80211vap *);
void	ieee80211_crypto_reload_keys(struct ieee80211com *);
void	ieee80211_crypto_set_deftxkey(struct ieee80211vap *,
	    ieee80211_keyix kid);

/*
 * Template for a supported cipher.  Ciphers register with the
 * crypto code and are typically loaded as separate modules
 * (the null cipher is always present).
 * XXX may need refcnts
 */
struct ieee80211_cipher {
	const char *ic_name;		/* printable name */
	u_int	ic_cipher;		/* IEEE80211_CIPHER_* */
	u_int	ic_header;		/* size of privacy header (bytes) */
	u_int	ic_trailer;		/* size of privacy trailer (bytes) */
	u_int	ic_miclen;		/* size of mic trailer (bytes) */
	void*	(*ic_attach)(struct ieee80211vap *, struct ieee80211_key *);
	void	(*ic_detach)(struct ieee80211_key *);
	int	(*ic_setkey)(struct ieee80211_key *);
	void	(*ic_setiv)(struct ieee80211_key *, uint8_t *);
	int	(*ic_encap)(struct ieee80211_key *, struct mbuf *);
	int	(*ic_decap)(struct ieee80211_key *, struct mbuf *, int);
	int	(*ic_enmic)(struct ieee80211_key *, struct mbuf *, int);
	int	(*ic_demic)(struct ieee80211_key *, struct mbuf *, int);
};
extern	const struct ieee80211_cipher ieee80211_cipher_none;

#define	IEEE80211_KEY_UNDEFINED(k) \
	((k)->wk_cipher == &ieee80211_cipher_none)

void	ieee80211_crypto_register(const struct ieee80211_cipher *);
void	ieee80211_crypto_unregister(const struct ieee80211_cipher *);
int	ieee80211_crypto_available(u_int cipher);

int	ieee80211_crypto_get_key_wepidx(const struct ieee80211vap *,
	    const struct ieee80211_key *k);
uint8_t	ieee80211_crypto_get_keyid(struct ieee80211vap *vap,
		struct ieee80211_key *k);
struct ieee80211_key *ieee80211_crypto_get_txkey(struct ieee80211_node *,
		struct mbuf *);
struct ieee80211_key *ieee80211_crypto_encap(struct ieee80211_node *,
		struct mbuf *);
int	ieee80211_crypto_decap(struct ieee80211_node *,
		struct mbuf *, int, struct ieee80211_key **);
int ieee80211_crypto_demic(struct ieee80211vap *vap, struct ieee80211_key *k,
		struct mbuf *, int);
/*
 * Add any MIC.
 */
static __inline int
ieee80211_crypto_enmic(struct ieee80211vap *vap,
	struct ieee80211_key *k, struct mbuf *m, int force)
{
	const struct ieee80211_cipher *cip = k->wk_cipher;
	return (cip->ic_miclen > 0 ? cip->ic_enmic(k, m, force) : 1);
}

/* 
 * Reset key state to an unused state.  The crypto
 * key allocation mechanism insures other state (e.g.
 * key data) is properly setup before a key is used.
 */
static __inline void
ieee80211_crypto_resetkey(struct ieee80211vap *vap,
	struct ieee80211_key *k, ieee80211_keyix ix)
{
	k->wk_cipher = &ieee80211_cipher_none;
	k->wk_private = k->wk_cipher->ic_attach(vap, k);
	k->wk_keyix = k->wk_rxkeyix = ix;
	k->wk_flags = IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV;
}

/*
 * Crypt-related notification methods.
 */
void	ieee80211_notify_replay_failure(struct ieee80211vap *,
		const struct ieee80211_frame *, const struct ieee80211_key *,
		uint64_t rsc, int tid);
void	ieee80211_notify_michael_failure(struct ieee80211vap *,
		const struct ieee80211_frame *, u_int keyix);
#endif /* defined(__KERNEL__) || defined(_KERNEL) */
#endif /* _NET80211_IEEE80211_CRYPTO_H_ */
