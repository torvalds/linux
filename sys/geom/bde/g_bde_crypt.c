/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/* This source file contains the functions responsible for the crypto, keying
 * and mapping operations on the I/O requests.
 *
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/endian.h>
#include <sys/md5.h>

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha512.h>

#include <geom/geom.h>
#include <geom/bde/g_bde.h>

/*
 * XXX: Debugging DO NOT ENABLE
 */
#undef MD5_KEY

/*
 * Derive kkey from mkey + sector offset.
 *
 * Security objective: Derive a potentially very large number of distinct skeys
 * from the comparatively small key material in our mkey, in such a way that
 * if one, more or even many of the kkeys are compromised, this does not
 * significantly help an attack on other kkeys and in particular does not
 * weaken or compromise the mkey.
 *
 * First we MD5 hash the sectornumber with the salt from the lock sector.
 * The salt prevents the precalculation and statistical analysis of the MD5
 * output which would be possible if we only gave it the sectornumber.
 *
 * The MD5 hash is used to pick out 16 bytes from the masterkey, which
 * are then hashed with MD5 together with the sector number.
 *
 * The resulting MD5 hash is the kkey.
 */

static void
g_bde_kkey(struct g_bde_softc *sc, keyInstance *ki, int dir, off_t sector)
{
	u_int t;
	MD5_CTX ct;
	u_char buf[16];
	u_char buf2[8];

	/* We have to be architecture neutral */
	le64enc(buf2, sector);

	MD5Init(&ct);
	MD5Update(&ct, sc->key.salt, 8);
	MD5Update(&ct, buf2, sizeof buf2);
	MD5Update(&ct, sc->key.salt + 8, 8);
	MD5Final(buf, &ct);

	MD5Init(&ct);
	for (t = 0; t < 16; t++) {
		MD5Update(&ct, &sc->key.mkey[buf[t]], 1);
		if (t == 8)
			MD5Update(&ct, buf2, sizeof buf2);
	}
	bzero(buf2, sizeof buf2);
	MD5Final(buf, &ct);
	bzero(&ct, sizeof ct);
	AES_makekey(ki, dir, G_BDE_KKEYBITS, buf);
	bzero(buf, sizeof buf);
}

/*
 * Encryption work for read operation.
 *
 * Security objective: Find the kkey, find the skey, decrypt the sector data.
 */

void
g_bde_crypt_read(struct g_bde_work *wp)
{
	struct g_bde_softc *sc;
	u_char *d;
	u_int n;
	off_t o;
	u_char skey[G_BDE_SKEYLEN];
	keyInstance ki;
	cipherInstance ci;
	

	AES_init(&ci);
	sc = wp->softc;
	o = 0;
	for (n = 0; o < wp->length; n++, o += sc->sectorsize) {
		d = (u_char *)wp->ksp->data + wp->ko + n * G_BDE_SKEYLEN;
		g_bde_kkey(sc, &ki, DIR_DECRYPT, wp->offset + o);
		AES_decrypt(&ci, &ki, d, skey, sizeof skey);
		d = (u_char *)wp->data + o;
		AES_makekey(&ki, DIR_DECRYPT, G_BDE_SKEYBITS, skey);
		AES_decrypt(&ci, &ki, d, d, sc->sectorsize);
	}
	bzero(skey, sizeof skey);
	bzero(&ci, sizeof ci);
	bzero(&ki, sizeof ki);
}

/*
 * Encryption work for write operation.
 *
 * Security objective: Create random skey, encrypt sector data,
 * encrypt skey with the kkey.
 */

void
g_bde_crypt_write(struct g_bde_work *wp)
{
	u_char *s, *d;
	struct g_bde_softc *sc;
	u_int n;
	off_t o;
	u_char skey[G_BDE_SKEYLEN];
	keyInstance ki;
	cipherInstance ci;

	sc = wp->softc;
	AES_init(&ci);
	o = 0;
	for (n = 0; o < wp->length; n++, o += sc->sectorsize) {

		s = (u_char *)wp->data + o;
		d = (u_char *)wp->sp->data + o;
		arc4rand(skey, sizeof skey, 0);
		AES_makekey(&ki, DIR_ENCRYPT, G_BDE_SKEYBITS, skey);
		AES_encrypt(&ci, &ki, s, d, sc->sectorsize);

		d = (u_char *)wp->ksp->data + wp->ko + n * G_BDE_SKEYLEN;
		g_bde_kkey(sc, &ki, DIR_ENCRYPT, wp->offset + o);
		AES_encrypt(&ci, &ki, skey, d, sizeof skey);
		bzero(skey, sizeof skey);
	}
	bzero(skey, sizeof skey);
	bzero(&ci, sizeof ci);
	bzero(&ki, sizeof ki);
}

/*
 * Encryption work for delete operation.
 *
 * Security objective: Write random data to the sectors.
 *
 * XXX: At a hit in performance we would trash the encrypted skey as well.
 * XXX: This would add frustration to the cleaning lady attack by making
 * XXX: deletes look like writes.
 */

void
g_bde_crypt_delete(struct g_bde_work *wp)
{
	struct g_bde_softc *sc;
	u_char *d;
	off_t o;
	u_char skey[G_BDE_SKEYLEN];
	keyInstance ki;
	cipherInstance ci;

	sc = wp->softc;
	d = wp->sp->data;
	AES_init(&ci);
	/*
	 * Do not unroll this loop!
	 * Our zone may be significantly wider than the amount of random
	 * bytes arc4rand likes to give in one reseeding, whereas our
	 * sectorsize is far more likely to be in the same range.
	 */
	for (o = 0; o < wp->length; o += sc->sectorsize) {
		arc4rand(d, sc->sectorsize, 0);
		arc4rand(skey, sizeof skey, 0);
		AES_makekey(&ki, DIR_ENCRYPT, G_BDE_SKEYBITS, skey);
		AES_encrypt(&ci, &ki, d, d, sc->sectorsize);
		d += sc->sectorsize;
	}
	/*
	 * Having written a long random sequence to disk here, we want to
	 * force a reseed, to avoid weakening the next time we use random
	 * data for something important.
	 */
	arc4rand(&o, sizeof o, 1);
}

/*
 * Calculate the total payload size of the encrypted device.
 *
 * Security objectives: none.
 *
 * This function needs to agree with g_bde_map_sector() about things.
 */

uint64_t
g_bde_max_sector(struct g_bde_key *kp)
{
	uint64_t maxsect;

	maxsect = kp->media_width;
	maxsect /= kp->zone_width;
	maxsect *= kp->zone_cont;
	return (maxsect);
}

/*
 * Convert an unencrypted side offset to offsets on the encrypted side.
 *
 * Security objective:  Make it harder to identify what sectors contain what
 * on a "cold" disk image.
 *
 * We do this by adding the "keyoffset" from the lock to the physical sector
 * number modulus the available number of sectors.  Since all physical sectors
 * presumably look the same cold, this will do.
 *
 * As part of the mapping we have to skip the lock sectors which we know
 * the physical address off.  We also truncate the work packet, respecting
 * zone boundaries and lock sectors, so that we end up with a sequence of
 * sectors which are physically contiguous.
 *
 * Shuffling things further is an option, but the incremental frustration is
 * not currently deemed worth the run-time performance hit resulting from the
 * increased number of disk arm movements it would incur.
 *
 * This function offers nothing but a trivial diversion for an attacker able
 * to do "the cleaning lady attack" in its current static mapping form.
 */

void
g_bde_map_sector(struct g_bde_work *wp)
{

	u_int	zone, zoff, u, len;
	uint64_t ko;
	struct g_bde_softc *sc;
	struct g_bde_key *kp;

	sc = wp->softc;
	kp = &sc->key;

	/* find which zone and the offset in it */
	zone = wp->offset / kp->zone_cont;
	zoff = wp->offset % kp->zone_cont;

	/* Calculate the offset of the key in the key sector */
	wp->ko = (zoff / kp->sectorsize) * G_BDE_SKEYLEN;

	/* restrict length to that zone */
	len = kp->zone_cont - zoff;

	/* ... and in general */
	if (len > DFLTPHYS)
		len = DFLTPHYS;

	if (len < wp->length)
		wp->length = len;

	/* Find physical sector address */
	wp->so = zone * kp->zone_width + zoff;
	wp->so += kp->keyoffset;
	wp->so %= kp->media_width;
	if (wp->so + wp->length > kp->media_width)
		wp->length = kp->media_width - wp->so;
	wp->so += kp->sector0;

	/* The key sector is the last in this zone. */
	wp->kso = zone * kp->zone_width + kp->zone_cont;
	wp->kso += kp->keyoffset;
	wp->kso %= kp->media_width;
	wp->kso += kp->sector0; 

	/* Compensate for lock sectors */
	for (u = 0; u < G_BDE_MAXKEYS; u++) {
		/* Find the start of this lock sector */
		ko = rounddown2(kp->lsector[u], (uint64_t)kp->sectorsize);

		if (wp->kso >= ko)
			wp->kso += kp->sectorsize;

		if (wp->so >= ko) {
			/* lock sector before work packet */
			wp->so += kp->sectorsize;
		} else if ((wp->so + wp->length) > ko) {
			/* lock sector in work packet, truncate */
			wp->length = ko - wp->so;
		}
	}

#if 0
	printf("off %jd len %jd so %jd ko %jd kso %u\n",
	    (intmax_t)wp->offset,
	    (intmax_t)wp->length,
	    (intmax_t)wp->so,
	    (intmax_t)wp->kso,
	    wp->ko);
#endif
	KASSERT(wp->so + wp->length <= kp->sectorN,
	    ("wp->so (%jd) + wp->length (%jd) > EOM (%jd), offset = %jd",
	    (intmax_t)wp->so,
	    (intmax_t)wp->length,
	    (intmax_t)kp->sectorN,
	    (intmax_t)wp->offset));

	KASSERT(wp->kso + kp->sectorsize <= kp->sectorN,
	    ("wp->kso (%jd) + kp->sectorsize > EOM (%jd), offset = %jd",
	    (intmax_t)wp->kso,
	    (intmax_t)kp->sectorN,
	    (intmax_t)wp->offset));

	KASSERT(wp->so >= kp->sector0,
	    ("wp->so (%jd) < BOM (%jd), offset = %jd",
	    (intmax_t)wp->so,
	    (intmax_t)kp->sector0,
	    (intmax_t)wp->offset));

	KASSERT(wp->kso >= kp->sector0,
	    ("wp->kso (%jd) <BOM (%jd), offset = %jd",
	    (intmax_t)wp->kso,
	    (intmax_t)kp->sector0,
	    (intmax_t)wp->offset));
}
