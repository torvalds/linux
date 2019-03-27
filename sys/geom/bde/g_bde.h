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

#ifndef _SYS_GEOM_BDE_G_BDE_H_
#define _SYS_GEOM_BDE_G_BDE_H_ 1

/*
 * These are quite, but not entirely unlike constants.
 *
 * They are not commented in details here, to prevent unadvisable
 * experimentation. Please consult the code where they are used before you
 * even think about modifying these.
 */

#define G_BDE_MKEYLEN	(2048/8)
#define G_BDE_SKEYBITS	128
#define G_BDE_SKEYLEN	(G_BDE_SKEYBITS/8)
#define G_BDE_KKEYBITS	128
#define G_BDE_KKEYLEN	(G_BDE_KKEYBITS/8)
#define G_BDE_MAXKEYS	4
#define G_BDE_LOCKSIZE	384
#define NLOCK_FIELDS	13


/* This just needs to be "large enough" */
#define G_BDE_KEYBYTES	304

struct g_bde_work;
struct g_bde_softc;

struct g_bde_sector {
	struct g_bde_work	*owner;
	struct g_bde_softc	*softc;
	off_t			offset;
	u_int			size;
	u_int			ref;
	void			*data;
	TAILQ_ENTRY(g_bde_sector) list;
	u_char			valid;
	u_char			malloc;
	enum {JUNK, IO, VALID}	state;
	int			error;
	time_t			used;
};

struct g_bde_work {
	struct mtx		mutex;
	off_t			offset;
	off_t			length;
	void			*data;
        struct bio      	*bp;
	struct g_bde_softc 	*softc;
        off_t           	so;
        off_t           	kso;
        u_int           	ko;
        struct g_bde_sector   	*sp;
        struct g_bde_sector   	*ksp;
	TAILQ_ENTRY(g_bde_work) list;
	enum {SETUP, WAIT, FINISH} state;
	int			error;
};

/*
 * The decrypted contents of the lock sectors.  Notice that this is not
 * the same as the on-disk layout.  The on-disk layout is dynamic and
 * dependent on the pass-phrase.
 */
struct g_bde_key {
	uint64_t		sector0;        
				/* Physical byte offset of 1st byte used */
	uint64_t		sectorN;
				/* Physical byte offset of 1st byte not used */
	uint64_t		keyoffset;
				/* Number of bytes the disk image is skewed. */
	uint64_t		lsector[G_BDE_MAXKEYS];
				/* Physical byte offsets of lock sectors */
	uint32_t		sectorsize;
				/* Our "logical" sector size */
	uint32_t		flags;
#define	GBDE_F_SECT0		1
	uint8_t			salt[16];
				/* Used to frustate the kkey generation */
	uint8_t			spare[32];
				/* For future use, random contents */
	uint8_t			mkey[G_BDE_MKEYLEN];
				/* Our masterkey. */

	/* Non-stored help-fields */
	uint64_t		zone_width;	/* On-disk width of zone */
	uint64_t		zone_cont;	/* Payload width of zone */
	uint64_t		media_width;	/* Non-magic width of zone */
	u_int			keys_per_sector;
};

struct g_bde_softc {
	off_t			mediasize;
	u_int			sectorsize;
	uint64_t		zone_cont;
	struct g_geom		*geom;
	struct g_consumer	*consumer;
	TAILQ_HEAD(, g_bde_sector)	freelist;
	TAILQ_HEAD(, g_bde_work) 	worklist;
	struct mtx		worklist_mutex;
	struct proc		*thread;
	struct g_bde_key	key;
	int			dead;
	u_int			nwork;
	u_int			nsect;
	u_int			ncache;
	u_char			sha2[SHA512_DIGEST_LENGTH];
};

/* g_bde_crypt.c */
void g_bde_crypt_delete(struct g_bde_work *wp);
void g_bde_crypt_read(struct g_bde_work *wp);
void g_bde_crypt_write(struct g_bde_work *wp);

/* g_bde_key.c */
void g_bde_zap_key(struct g_bde_softc *sc);
int g_bde_get_key(struct g_bde_softc *sc, void *ptr, int len);
int g_bde_init_keybytes(struct g_bde_softc *sc, char *passp, int len);

/* g_bde_lock .c */
int g_bde_encode_lock(u_char *sha2, struct g_bde_key *gl, u_char *ptr);
int g_bde_decode_lock(struct g_bde_softc *sc, struct g_bde_key *gl, u_char *ptr);
int g_bde_keyloc_encrypt(u_char *sha2, uint64_t v0, uint64_t v1, void *output);
int g_bde_keyloc_decrypt(u_char *sha2, void *input, uint64_t *output);
int g_bde_decrypt_lock(struct g_bde_softc *sc, u_char *keymat, u_char *meta, off_t mediasize, u_int sectorsize, u_int *nkey);
void g_bde_hash_pass(struct g_bde_softc *sc, const void *input, u_int len);

/* g_bde_math .c */
uint64_t g_bde_max_sector(struct g_bde_key *lp);
void g_bde_map_sector(struct g_bde_work *wp);

/* g_bde_work.c */
void g_bde_start1(struct bio *bp);
void g_bde_worker(void *arg);

/*
 * These four functions wrap the raw Rijndael functions and make sure we
 * explode if something fails which shouldn't.
 */

static __inline void
AES_init(cipherInstance *ci)
{
	int error;

	error = rijndael_cipherInit(ci, MODE_CBC, NULL);
	KASSERT(error > 0, ("rijndael_cipherInit %d", error));
}

static __inline void
AES_makekey(keyInstance *ki, int dir, u_int len, const void *key)
{
	int error;

	error = rijndael_makeKey(ki, dir, len, key);
	KASSERT(error > 0, ("rijndael_makeKey %d", error));
}

static __inline void
AES_encrypt(cipherInstance *ci, keyInstance *ki, const void *in, void *out, u_int len)
{
	int error;

	error = rijndael_blockEncrypt(ci, ki, in, len * 8, out);
	KASSERT(error > 0, ("rijndael_blockEncrypt %d", error));
}

static __inline void
AES_decrypt(cipherInstance *ci, keyInstance *ki, const void *in, void *out, u_int len)
{
	int error;

	error = rijndael_blockDecrypt(ci, ki, in, len * 8, out);
	KASSERT(error > 0, ("rijndael_blockDecrypt %d", error));
}

#endif /* _SYS_GEOM_BDE_G_BDE_H_ */
