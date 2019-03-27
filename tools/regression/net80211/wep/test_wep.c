/*-
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
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

/*
 * WEP test module.
 *
 * Test vectors come from section I.7.2 of P802.11i/D7.0, October 2003.
 *
 * To use this tester load the net80211 layer (either as a module or
 * by statically configuring it into your kernel), then insmod this
 * module.  It should automatically run all test cases and print
 * information for each.  To run one or more tests you can specify a
 * tests parameter to the module that is a bit mask of the set of tests
 * you want; e.g. insmod wep_test tests=7 will run only test mpdu's
 * 1, 2, and 3.
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/module.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

/*
MPDU data
 aa aa 03 00 00 00 08 00 45 00 00 4e 66 1a 00 00 80 11 be 64 0a 00 01 22
 0a ff ff ff 00 89 00 89 00 3a 00 00 80 a6 01 10 00 01 00 00 00 00 00 00
 20 45 43 45 4a 45 48 45 43 46 43 45 50 46 45 45 49 45 46 46 43 43 41 43
 41 43 41 43 41 43 41 41 41 00 00 20 00 01

RC4 encryption is performed as follows:
17
18  Key  fb 02 9e 30 31 32 33 34 
Plaintext
 aa aa 03 00 00 00 08 00 45 00 00 4e 66 1a 00 00 80 11 be 64 0a 00 01
 22 0a ff ff ff 00 89 00 89 00 3a 00 00 80 a6 01 10 00 01 00 00 00 00
 00 00 20 45 43 45 4a 45 48 45 43 46 43 45 50 46 45 45 49 45 46 46 43
 43 41 43 41 43 41 43 41 43 41 41 41 00 00 20 00 01 1b d0 b6 04
Ciphertext
 f6 9c 58 06 bd 6c e8 46 26 bc be fb 94 74 65 0a ad 1f 79 09 b0 f6 4d
 5f 58 a5 03 a2 58 b7 ed 22 eb 0e a6 49 30 d3 a0 56 a5 57 42 fc ce 14
 1d 48 5f 8a a8 36 de a1 8d f4 2c 53 80 80 5a d0 c6 1a 5d 6f 58 f4 10
 40 b2 4b 7d 1a 69 38 56 ed 0d 43 98 e7 ae e3 bf 0e 2a 2c a8 f7
The plaintext consists of the MPDU data, followed by a 4-octet CRC-32
calculated over the MPDU data.
19  The expanded MPDU, after WEP encapsulation, is as follows:
20
21  IV  fb 02 9e 80
MPDU  data
 f6 9c 58 06 bd 6c e8 46 26 bc be fb 94 74 65 0a ad 1f 79 09 b0 f6 4d 5f 58 a5
 03 a2 58 b7 ed 22 eb 0e a6 49 30 d3 a0 56 a5 57 42 fc ce 14 1d 48 5f 8a a8 36
 de a1 8d f4 2c 53 80 80 5a d0 c6 1a 5d 6f 58 f4 10 40 b2 4b 7d 1a 69 38 56 ed
 0d 43 98 e7 ae e3 bf 0e
ICV  2a 2c a8 f7  
*/
static const u_int8_t test1_key[] = {		/* TK (w/o IV) */
	0x30, 0x31, 0x32, 0x33, 0x34, 
};
static const u_int8_t test1_plaintext[] = {	/* Plaintext MPDU */
	0x08, 0x48, 0xc3, 0x2c, 0x0f, 0xd2, 0xe1, 0x28,	/* 802.11 Header */
	0xa5, 0x7c, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08,
	0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba, 0x80, 0x33, 
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,	/* Plaintext data */
	0x45, 0x00, 0x00, 0x4e, 0x66, 0x1a, 0x00, 0x00,
	0x80, 0x11, 0xbe, 0x64, 0x0a, 0x00, 0x01, 0x22,
	0x0a, 0xff, 0xff, 0xff, 0x00, 0x89, 0x00, 0x89,
	0x00, 0x3a, 0x00, 0x00, 0x80, 0xa6, 0x01, 0x10,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x20, 0x45, 0x43, 0x45, 0x4a, 0x45, 0x48, 0x45,
	0x43, 0x46, 0x43, 0x45, 0x50, 0x46, 0x45, 0x45,
	0x49, 0x45, 0x46, 0x46, 0x43, 0x43, 0x41, 0x43,
	0x41, 0x43, 0x41, 0x43, 0x41, 0x43, 0x41, 0x41,
	0x41, 0x00, 0x00, 0x20, 0x00, 0x01,
};
static const u_int8_t test1_encrypted[] = {	/* Encrypted MPDU */
	0x08, 0x48, 0xc3, 0x2c, 0x0f, 0xd2, 0xe1, 0x28,
	0xa5, 0x7c, 0x50, 0x30, 0xf1, 0x84, 0x44, 0x08,
	0xab, 0xae, 0xa5, 0xb8, 0xfc, 0xba, 0x80, 0x33,
	0xfb, 0x02, 0x9e, 0x80, 0xf6, 0x9c, 0x58, 0x06,
	0xbd, 0x6c, 0xe8, 0x46, 0x26, 0xbc, 0xbe, 0xfb,
	0x94, 0x74, 0x65, 0x0a, 0xad, 0x1f, 0x79, 0x09,
	0xb0, 0xf6, 0x4d, 0x5f, 0x58, 0xa5, 0x03, 0xa2,
	0x58, 0xb7, 0xed, 0x22, 0xeb, 0x0e, 0xa6, 0x49,
	0x30, 0xd3, 0xa0, 0x56, 0xa5, 0x57, 0x42, 0xfc,
	0xce, 0x14, 0x1d, 0x48, 0x5f, 0x8a, 0xa8, 0x36,
	0xde, 0xa1, 0x8d, 0xf4, 0x2c, 0x53, 0x80, 0x80,
	0x5a, 0xd0, 0xc6, 0x1a, 0x5d, 0x6f, 0x58, 0xf4,
	0x10, 0x40, 0xb2, 0x4b, 0x7d, 0x1a, 0x69, 0x38,
	0x56, 0xed, 0x0d, 0x43, 0x98, 0xe7, 0xae, 0xe3,
	0xbf, 0x0e, 0x2a, 0x2c, 0xa8, 0xf7,
};

/* XXX fix byte order of iv */
#define	TEST(n,name,cipher,keyix,iv0,iv1,iv2,iv3) { \
	name, IEEE80211_CIPHER_##cipher,keyix, { iv2,iv1,iv0,iv3 }, \
	test##n##_key,   sizeof(test##n##_key), \
	test##n##_plaintext, sizeof(test##n##_plaintext), \
	test##n##_encrypted, sizeof(test##n##_encrypted) \
}

struct ciphertest {
	const char	*name;
	int		cipher;
	int		keyix;
	u_int8_t	iv[4];
	const u_int8_t	*key;
	size_t		key_len;
	const u_int8_t	*plaintext;
	size_t		plaintext_len;
	const u_int8_t	*encrypted;
	size_t		encrypted_len;
} weptests[] = {
	TEST(1, "WEP test mpdu 1", WEP, 2, 0xfb, 0x02, 0x9e, 0x80),
};

static void
dumpdata(const char *tag, const void *p, size_t len)
{
	int i;

	printf("%s: 0x%p len %u", tag, p, len);
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printf("\n%03d:", i);
		printf(" %02x", ((const u_int8_t *)p)[i]);
	}
	printf("\n");
}

static void
cmpfail(const void *gen, size_t genlen, const void *ref, size_t reflen)
{
	int i;

	for (i = 0; i < genlen; i++)
		if (((const u_int8_t *)gen)[i] != ((const u_int8_t *)ref)[i]) {
			printf("first difference at byte %u\n", i);
			break;
		}
	dumpdata("Generated", gen, genlen);
	dumpdata("Reference", ref, reflen);
}

struct wep_ctx_hw {			/* for use with h/w support */
	struct ieee80211vap *wc_vap;	/* for diagnostics+statistics */
	struct ieee80211com *wc_ic;
	uint32_t        wc_iv;		/* initial vector for crypto */
};

static int
runtest(struct ieee80211vap *vap, struct ciphertest *t)
{
	struct ieee80211_key *key = &vap->iv_nw_keys[t->keyix];
	struct mbuf *m = NULL;
	const struct ieee80211_cipher *cip;
	struct wep_ctx_hw *ctx;
	int hdrlen;

	printf("%s: ", t->name);

	/*
	 * Setup key.
	 */
	memset(key, 0, sizeof(*key));
	key->wk_flags = IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV;
	key->wk_cipher = &ieee80211_cipher_none;
	if (!ieee80211_crypto_newkey(vap, t->cipher,
	    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV, key)) {
		printf("FAIL: ieee80211_crypto_newkey failed\n");
		goto bad;
	}

	memcpy(key->wk_key, t->key, t->key_len);
	key->wk_keylen = t->key_len;
	if (!ieee80211_crypto_setkey(vap, key)) {
		printf("FAIL: ieee80211_crypto_setkey failed\n");
		goto bad;
	}

	/*
	 * Craft frame from plaintext data.
	 */
	cip = key->wk_cipher;
	m = m_getcl(M_NOWAIT, MT_HEADER, M_PKTHDR);
	memcpy(mtod(m, void *), t->encrypted, t->encrypted_len);
	m->m_len = t->encrypted_len;
	m->m_pkthdr.len = m->m_len;
	hdrlen = ieee80211_anyhdrsize(mtod(m, void *));

	/*
	 * Decrypt frame.
	 */
	if (!cip->ic_decap(key, m, hdrlen)) {
		printf("FAIL: wep decap failed\n");
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->plaintext, t->plaintext_len);
		goto bad;
	}
	/*
	 * Verify: frame length, frame contents.
	 */
	if (m->m_pkthdr.len != t->plaintext_len) {
		printf("FAIL: decap botch; length mismatch\n");
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->plaintext, t->plaintext_len);
		goto bad;
	} else if (memcmp(mtod(m, const void *), t->plaintext, t->plaintext_len)) {
		printf("FAIL: decap botch; data does not compare\n");
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->plaintext, t->plaintext_len);
		goto bad;
	}

	/*
	 * Encrypt frame.
	 */
	ctx = (struct wep_ctx_hw *) key->wk_private;
	ctx->wc_vap = vap;
	ctx->wc_ic = vap->iv_ic;
	memcpy(&ctx->wc_iv, t->iv, sizeof(t->iv));	/* for encap/encrypt */
	if (!cip->ic_encap(key, m)) {
		printf("FAIL: wep encap failed\n");
		goto bad;
	}
	/*
	 * Verify: frame length, frame contents.
	 */
	if (m->m_pkthdr.len != t->encrypted_len) {
		printf("FAIL: encap data length mismatch\n");
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->encrypted, t->encrypted_len);
		goto bad;
	} else if (memcmp(mtod(m, const void *), t->encrypted, m->m_pkthdr.len)) {
		printf("FAIL: encrypt data does not compare\n");
		cmpfail(mtod(m, const void *), m->m_pkthdr.len,
			t->encrypted, t->encrypted_len);
		dumpdata("Plaintext", t->plaintext, t->plaintext_len);
		goto bad;
	}
	m_freem(m);
	ieee80211_crypto_delkey(vap, key);
	printf("PASS\n");
	return 1;
bad:
	if (m != NULL)
		m_freem(m);
	ieee80211_crypto_delkey(vap, key);
	return 0;
}

/*
 * Module glue.
 */

static	int tests = -1;
static	int debug = 0;

static int
init_crypto_wep_test(void)
{
	struct ieee80211com ic;
	struct ieee80211vap vap;
	struct ifnet ifp;
	int i, pass, total;

	memset(&ic, 0, sizeof(ic));
	memset(&vap, 0, sizeof(vap));
	memset(&ifp, 0, sizeof(ifp));

	ieee80211_crypto_attach(&ic);

	/* some minimal initialization */
	strncpy(ifp.if_xname, "test_ccmp", sizeof(ifp.if_xname));
	vap.iv_ic = &ic;
	vap.iv_ifp = &ifp;
	if (debug)
		vap.iv_debug = IEEE80211_MSG_CRYPTO;
	ieee80211_crypto_vattach(&vap);

	pass = 0;
	total = 0;
	for (i = 0; i < nitems(weptests); i++)
		if (tests & (1<<i)) {
			total++;
			pass += runtest(&vap, &weptests[i]);
		}
	printf("%u of %u 802.11i WEP test vectors passed\n", pass, total);

	ieee80211_crypto_vdetach(&vap);
	ieee80211_crypto_detach(&ic);

	return (pass == total ? 0 : -1);
}

static int
test_wep_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		(void) init_crypto_wep_test();
		return 0;
	case MOD_UNLOAD:
		return 0;
	}
	return EINVAL;
}

static moduledata_t test_wep_mod = {
	"test_wep",
	test_wep_modevent,
	0
};
DECLARE_MODULE(test_wep, test_wep_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(test_wep, 1);
MODULE_DEPEND(test_wep, wlan, 1, 1, 1);
