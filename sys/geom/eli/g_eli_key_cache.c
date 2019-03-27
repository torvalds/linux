/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#endif /* _KERNEL */
#include <sys/queue.h>
#include <sys/tree.h>

#include <geom/geom.h>

#include <geom/eli/g_eli.h>

#ifdef _KERNEL
MALLOC_DECLARE(M_ELI);

SYSCTL_DECL(_kern_geom_eli);
/*
 * The default limit (8192 keys) will allow to cache all keys for 4TB
 * provider with 512 bytes sectors and will take around 1MB of memory.
 */
static u_int g_eli_key_cache_limit = 8192;
SYSCTL_UINT(_kern_geom_eli, OID_AUTO, key_cache_limit, CTLFLAG_RDTUN,
    &g_eli_key_cache_limit, 0, "Maximum number of encryption keys to cache");
static uint64_t g_eli_key_cache_hits;
SYSCTL_UQUAD(_kern_geom_eli, OID_AUTO, key_cache_hits, CTLFLAG_RW,
    &g_eli_key_cache_hits, 0, "Key cache hits");
static uint64_t g_eli_key_cache_misses;
SYSCTL_UQUAD(_kern_geom_eli, OID_AUTO, key_cache_misses, CTLFLAG_RW,
    &g_eli_key_cache_misses, 0, "Key cache misses");

static int
g_eli_key_cmp(const struct g_eli_key *a, const struct g_eli_key *b)
{

	if (a->gek_keyno > b->gek_keyno)
		return (1);
	else if (a->gek_keyno < b->gek_keyno)
		return (-1);
	return (0);
}
#endif /* _KERNEL */

void
g_eli_key_fill(struct g_eli_softc *sc, struct g_eli_key *key, uint64_t keyno)
{
	const uint8_t *ekey;
	struct {
		char magic[4];
		uint8_t keyno[8];
	} __packed hmacdata;

	if ((sc->sc_flags & G_ELI_FLAG_ENC_IVKEY) != 0)
		ekey = sc->sc_mkey;
	else
		ekey = sc->sc_ekey;

	bcopy("ekey", hmacdata.magic, 4);
	le64enc(hmacdata.keyno, keyno);
	g_eli_crypto_hmac(ekey, G_ELI_MAXKEYLEN, (uint8_t *)&hmacdata,
	    sizeof(hmacdata), key->gek_key, 0);
	key->gek_keyno = keyno;
	key->gek_count = 0;
	key->gek_magic = G_ELI_KEY_MAGIC;
}

#ifdef _KERNEL
RB_PROTOTYPE(g_eli_key_tree, g_eli_key, gek_link, g_eli_key_cmp);
RB_GENERATE(g_eli_key_tree, g_eli_key, gek_link, g_eli_key_cmp);

static struct g_eli_key *
g_eli_key_allocate(struct g_eli_softc *sc, uint64_t keyno)
{
	struct g_eli_key *key, *ekey, keysearch;

	mtx_assert(&sc->sc_ekeys_lock, MA_OWNED);
	mtx_unlock(&sc->sc_ekeys_lock);

	key = malloc(sizeof(*key), M_ELI, M_WAITOK);
	g_eli_key_fill(sc, key, keyno);

	mtx_lock(&sc->sc_ekeys_lock);
	/*
	 * Recheck if the key wasn't added while we weren't holding the lock.
	 */
	keysearch.gek_keyno = keyno;
	ekey = RB_FIND(g_eli_key_tree, &sc->sc_ekeys_tree, &keysearch);
	if (ekey != NULL) {
		explicit_bzero(key, sizeof(*key));
		free(key, M_ELI);
		key = ekey;
		TAILQ_REMOVE(&sc->sc_ekeys_queue, key, gek_next);
	} else {
		RB_INSERT(g_eli_key_tree, &sc->sc_ekeys_tree, key);
		sc->sc_ekeys_allocated++;
	}
	TAILQ_INSERT_TAIL(&sc->sc_ekeys_queue, key, gek_next);

	return (key);
}

static struct g_eli_key *
g_eli_key_find_last(struct g_eli_softc *sc)
{
	struct g_eli_key *key;

	mtx_assert(&sc->sc_ekeys_lock, MA_OWNED);

	TAILQ_FOREACH(key, &sc->sc_ekeys_queue, gek_next) {
		if (key->gek_count == 0)
			break;
	}

	return (key);
}

static void
g_eli_key_replace(struct g_eli_softc *sc, struct g_eli_key *key, uint64_t keyno)
{

	mtx_assert(&sc->sc_ekeys_lock, MA_OWNED);
	KASSERT(key->gek_magic == G_ELI_KEY_MAGIC, ("Invalid magic."));

	RB_REMOVE(g_eli_key_tree, &sc->sc_ekeys_tree, key);
	TAILQ_REMOVE(&sc->sc_ekeys_queue, key, gek_next);

	KASSERT(key->gek_count == 0, ("gek_count=%d", key->gek_count));

	g_eli_key_fill(sc, key, keyno);

	RB_INSERT(g_eli_key_tree, &sc->sc_ekeys_tree, key);
	TAILQ_INSERT_TAIL(&sc->sc_ekeys_queue, key, gek_next);
}

static void
g_eli_key_remove(struct g_eli_softc *sc, struct g_eli_key *key)
{

	mtx_assert(&sc->sc_ekeys_lock, MA_OWNED);
	KASSERT(key->gek_magic == G_ELI_KEY_MAGIC, ("Invalid magic."));
	KASSERT(key->gek_count == 0, ("gek_count=%d", key->gek_count));

	RB_REMOVE(g_eli_key_tree, &sc->sc_ekeys_tree, key);
	TAILQ_REMOVE(&sc->sc_ekeys_queue, key, gek_next);
	sc->sc_ekeys_allocated--;
	explicit_bzero(key, sizeof(*key));
	free(key, M_ELI);
}

void
g_eli_key_init(struct g_eli_softc *sc)
{
	uint8_t *mkey;

	mtx_lock(&sc->sc_ekeys_lock);

	mkey = sc->sc_mkey + sizeof(sc->sc_ivkey);
	if ((sc->sc_flags & G_ELI_FLAG_AUTH) == 0)
		bcopy(mkey, sc->sc_ekey, G_ELI_DATAKEYLEN);
	else {
		/*
		 * The encryption key is: ekey = HMAC_SHA512(Data-Key, 0x10)
		 */
		g_eli_crypto_hmac(mkey, G_ELI_MAXKEYLEN, "\x10", 1,
		    sc->sc_ekey, 0);
	}

	if ((sc->sc_flags & G_ELI_FLAG_SINGLE_KEY) != 0) {
		sc->sc_ekeys_total = 1;
		sc->sc_ekeys_allocated = 0;
	} else {
		off_t mediasize;
		size_t blocksize;

		if ((sc->sc_flags & G_ELI_FLAG_AUTH) != 0) {
			struct g_provider *pp;

			pp = LIST_FIRST(&sc->sc_geom->consumer)->provider;
			mediasize = pp->mediasize;
			blocksize = pp->sectorsize;
		} else {
			mediasize = sc->sc_mediasize;
			blocksize = sc->sc_sectorsize;
		}
		sc->sc_ekeys_total =
		    ((mediasize - 1) >> G_ELI_KEY_SHIFT) / blocksize + 1;
		sc->sc_ekeys_allocated = 0;
		TAILQ_INIT(&sc->sc_ekeys_queue);
		RB_INIT(&sc->sc_ekeys_tree);
		if (sc->sc_ekeys_total <= g_eli_key_cache_limit) {
			uint64_t keyno;

			for (keyno = 0; keyno < sc->sc_ekeys_total; keyno++)
				(void)g_eli_key_allocate(sc, keyno);
			KASSERT(sc->sc_ekeys_total == sc->sc_ekeys_allocated,
			    ("sc_ekeys_total=%ju != sc_ekeys_allocated=%ju",
			    (uintmax_t)sc->sc_ekeys_total,
			    (uintmax_t)sc->sc_ekeys_allocated));
		}
	}

	mtx_unlock(&sc->sc_ekeys_lock);
}

void
g_eli_key_destroy(struct g_eli_softc *sc)
{

	mtx_lock(&sc->sc_ekeys_lock);
	if ((sc->sc_flags & G_ELI_FLAG_SINGLE_KEY) != 0) {
		explicit_bzero(sc->sc_ekey, sizeof(sc->sc_ekey));
	} else {
		struct g_eli_key *key;

		while ((key = TAILQ_FIRST(&sc->sc_ekeys_queue)) != NULL)
			g_eli_key_remove(sc, key);
		TAILQ_INIT(&sc->sc_ekeys_queue);
		RB_INIT(&sc->sc_ekeys_tree);
	}
	mtx_unlock(&sc->sc_ekeys_lock);
}

/*
 * Select encryption key. If G_ELI_FLAG_SINGLE_KEY is present we only have one
 * key available for all the data. If the flag is not present select the key
 * based on data offset.
 */
uint8_t *
g_eli_key_hold(struct g_eli_softc *sc, off_t offset, size_t blocksize)
{
	struct g_eli_key *key, keysearch;
	uint64_t keyno;

	if ((sc->sc_flags & G_ELI_FLAG_SINGLE_KEY) != 0)
		return (sc->sc_ekey);

	/* We switch key every 2^G_ELI_KEY_SHIFT blocks. */
	keyno = (offset >> G_ELI_KEY_SHIFT) / blocksize;

	KASSERT(keyno < sc->sc_ekeys_total,
	    ("%s: keyno=%ju >= sc_ekeys_total=%ju",
	    __func__, (uintmax_t)keyno, (uintmax_t)sc->sc_ekeys_total));

	keysearch.gek_keyno = keyno;

	if (sc->sc_ekeys_total == sc->sc_ekeys_allocated) {
		/* We have all the keys, so avoid some overhead. */
		key = RB_FIND(g_eli_key_tree, &sc->sc_ekeys_tree, &keysearch);
		KASSERT(key != NULL, ("No key %ju found.", (uintmax_t)keyno));
		KASSERT(key->gek_magic == G_ELI_KEY_MAGIC,
		    ("Invalid key magic."));
		return (key->gek_key);
	}

	mtx_lock(&sc->sc_ekeys_lock);
	key = RB_FIND(g_eli_key_tree, &sc->sc_ekeys_tree, &keysearch);
	if (key != NULL) {
		g_eli_key_cache_hits++;
		TAILQ_REMOVE(&sc->sc_ekeys_queue, key, gek_next);
		TAILQ_INSERT_TAIL(&sc->sc_ekeys_queue, key, gek_next);
	} else {
		/*
		 * No key in cache, find the least recently unreferenced key
		 * or allocate one if we haven't reached our limit yet.
		 */
		if (sc->sc_ekeys_allocated < g_eli_key_cache_limit) {
			key = g_eli_key_allocate(sc, keyno);
		} else {
			g_eli_key_cache_misses++;
			key = g_eli_key_find_last(sc);
			if (key != NULL) {
				g_eli_key_replace(sc, key, keyno);
			} else {
				/* All keys are referenced? Allocate one. */
				key = g_eli_key_allocate(sc, keyno);
			}
		}
	}
	key->gek_count++;
	mtx_unlock(&sc->sc_ekeys_lock);

	KASSERT(key->gek_magic == G_ELI_KEY_MAGIC, ("Invalid key magic."));

	return (key->gek_key);
}

void
g_eli_key_drop(struct g_eli_softc *sc, uint8_t *rawkey)
{
	struct g_eli_key *key = (struct g_eli_key *)rawkey;

	if ((sc->sc_flags & G_ELI_FLAG_SINGLE_KEY) != 0)
		return;

	KASSERT(key->gek_magic == G_ELI_KEY_MAGIC, ("Invalid key magic."));

	if (sc->sc_ekeys_total == sc->sc_ekeys_allocated)
		return;

	mtx_lock(&sc->sc_ekeys_lock);
	KASSERT(key->gek_count > 0, ("key->gek_count=%d", key->gek_count));
	key->gek_count--;
	while (sc->sc_ekeys_allocated > g_eli_key_cache_limit) {
		key = g_eli_key_find_last(sc);
		if (key == NULL)
			break;
		g_eli_key_remove(sc, key);
	}
	mtx_unlock(&sc->sc_ekeys_lock);
}
#endif /* _KERNEL */
