/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
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
 *
 * $FreeBSD$
 */

#ifndef	_G_ELI_H_
#define	_G_ELI_H_

#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <crypto/sha2/sha256.h>
#include <crypto/sha2/sha512.h>
#include <opencrypto/cryptodev.h>
#ifdef _KERNEL
#include <sys/bio.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <geom/geom.h>
#include <crypto/intake.h>
#else
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#endif
#include <sys/queue.h>
#include <sys/tree.h>
#ifndef _OpenSSL_
#include <sys/md5.h>
#endif

#define	G_ELI_CLASS_NAME	"ELI"
#define	G_ELI_MAGIC		"GEOM::ELI"
#define	G_ELI_SUFFIX		".eli"

/*
 * Version history:
 * 0 - Initial version number.
 * 1 - Added data authentication support (md_aalgo field and
 *     G_ELI_FLAG_AUTH flag).
 * 2 - Added G_ELI_FLAG_READONLY.
 * 3 - Added 'configure' subcommand.
 * 4 - IV is generated from offset converted to little-endian
 *     (the G_ELI_FLAG_NATIVE_BYTE_ORDER flag will be set for older versions).
 * 5 - Added multiple encrypton keys and AES-XTS support.
 * 6 - Fixed usage of multiple keys for authenticated providers (the
 *     G_ELI_FLAG_FIRST_KEY flag will be set for older versions).
 * 7 - Encryption keys are now generated from the Data Key and not from the
 *     IV Key (the G_ELI_FLAG_ENC_IVKEY flag will be set for older versions).
 */
#define	G_ELI_VERSION_00	0
#define	G_ELI_VERSION_01	1
#define	G_ELI_VERSION_02	2
#define	G_ELI_VERSION_03	3
#define	G_ELI_VERSION_04	4
#define	G_ELI_VERSION_05	5
#define	G_ELI_VERSION_06	6
#define	G_ELI_VERSION_07	7
#define	G_ELI_VERSION		G_ELI_VERSION_07

/* ON DISK FLAGS. */
/* Use random, onetime keys. */
#define	G_ELI_FLAG_ONETIME		0x00000001
/* Ask for the passphrase from the kernel, before mounting root. */
#define	G_ELI_FLAG_BOOT			0x00000002
/* Detach on last close, if we were open for writing. */
#define	G_ELI_FLAG_WO_DETACH		0x00000004
/* Detach on last close. */
#define	G_ELI_FLAG_RW_DETACH		0x00000008
/* Provide data authentication. */
#define	G_ELI_FLAG_AUTH			0x00000010
/* Provider is read-only, we should deny all write attempts. */
#define	G_ELI_FLAG_RO			0x00000020
/* Don't pass through BIO_DELETE requests. */
#define	G_ELI_FLAG_NODELETE		0x00000040
/* This GELI supports GELIBoot */
#define	G_ELI_FLAG_GELIBOOT		0x00000080
/* Hide passphrase length in GELIboot. */
#define	G_ELI_FLAG_GELIDISPLAYPASS	0x00000100
/* RUNTIME FLAGS. */
/* Provider was open for writing. */
#define	G_ELI_FLAG_WOPEN		0x00010000
/* Destroy device. */
#define	G_ELI_FLAG_DESTROY		0x00020000
/* Provider uses native byte-order for IV generation. */
#define	G_ELI_FLAG_NATIVE_BYTE_ORDER	0x00040000
/* Provider uses single encryption key. */
#define	G_ELI_FLAG_SINGLE_KEY		0x00080000
/* Device suspended. */
#define	G_ELI_FLAG_SUSPEND		0x00100000
/* Provider uses first encryption key. */
#define	G_ELI_FLAG_FIRST_KEY		0x00200000
/* Provider uses IV-Key for encryption key generation. */
#define	G_ELI_FLAG_ENC_IVKEY		0x00400000

#define	G_ELI_NEW_BIO	255

#define	SHA512_MDLEN		64
#define	G_ELI_AUTH_SECKEYLEN	SHA256_DIGEST_LENGTH

#define	G_ELI_MAXMKEYS		2
#define	G_ELI_MAXKEYLEN		64
#define	G_ELI_USERKEYLEN	G_ELI_MAXKEYLEN
#define	G_ELI_DATAKEYLEN	G_ELI_MAXKEYLEN
#define	G_ELI_AUTHKEYLEN	G_ELI_MAXKEYLEN
#define	G_ELI_IVKEYLEN		G_ELI_MAXKEYLEN
#define	G_ELI_SALTLEN		64
#define	G_ELI_DATAIVKEYLEN	(G_ELI_DATAKEYLEN + G_ELI_IVKEYLEN)
/* Data-Key, IV-Key, HMAC_SHA512(Derived-Key, Data-Key+IV-Key) */
#define	G_ELI_MKEYLEN		(G_ELI_DATAIVKEYLEN + SHA512_MDLEN)
#define	G_ELI_OVERWRITES	5
/* Switch data encryption key every 2^20 blocks. */
#define	G_ELI_KEY_SHIFT		20

#define	G_ELI_CRYPTO_UNKNOWN	0
#define	G_ELI_CRYPTO_HW		1
#define	G_ELI_CRYPTO_SW		2

#ifdef _KERNEL
#if (MAX_KEY_BYTES < G_ELI_DATAIVKEYLEN)
#error "MAX_KEY_BYTES is less than G_ELI_DATAKEYLEN"
#endif

extern int g_eli_debug;
extern u_int g_eli_overwrites;
extern u_int g_eli_batch;

#define	G_ELI_DEBUG(lvl, ...)	do {					\
	if (g_eli_debug >= (lvl)) {					\
		printf("GEOM_ELI");					\
		if (g_eli_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)
#define	G_ELI_LOGREQ(lvl, bp, ...)	do {				\
	if (g_eli_debug >= (lvl)) {					\
		printf("GEOM_ELI");					\
		if (g_eli_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

struct g_eli_worker {
	struct g_eli_softc	*w_softc;
	struct proc		*w_proc;
	u_int			 w_number;
	crypto_session_t	 w_sid;
	boolean_t		 w_active;
	LIST_ENTRY(g_eli_worker) w_next;
};

#endif	/* _KERNEL */

struct g_eli_softc {
	struct g_geom	*sc_geom;
	u_int		 sc_version;
	u_int		 sc_crypto;
	uint8_t		 sc_mkey[G_ELI_DATAIVKEYLEN];
	uint8_t		 sc_ekey[G_ELI_DATAKEYLEN];
	TAILQ_HEAD(, g_eli_key) sc_ekeys_queue;
	RB_HEAD(g_eli_key_tree, g_eli_key) sc_ekeys_tree;
	struct mtx	 sc_ekeys_lock;
	uint64_t	 sc_ekeys_total;
	uint64_t	 sc_ekeys_allocated;
	u_int		 sc_ealgo;
	u_int		 sc_ekeylen;
	uint8_t		 sc_akey[G_ELI_AUTHKEYLEN];
	u_int		 sc_aalgo;
	u_int		 sc_akeylen;
	u_int		 sc_alen;
	SHA256_CTX	 sc_akeyctx;
	uint8_t		 sc_ivkey[G_ELI_IVKEYLEN];
	SHA256_CTX	 sc_ivctx;
	int		 sc_nkey;
	uint32_t	 sc_flags;
	int		 sc_inflight;
	off_t		 sc_mediasize;
	size_t		 sc_sectorsize;
	u_int		 sc_bytes_per_sector;
	u_int		 sc_data_per_sector;
#ifndef _KERNEL
	int		 sc_cpubind;
#else /* _KERNEL */
	boolean_t	 sc_cpubind;

	/* Only for software cryptography. */
	struct bio_queue_head sc_queue;
	struct mtx	 sc_queue_mtx;
	LIST_HEAD(, g_eli_worker) sc_workers;
#endif /* _KERNEL */
};
#define	sc_name		 sc_geom->name

#define	G_ELI_KEY_MAGIC	0xe11341c

struct g_eli_key {
	/* Key value, must be first in the structure. */
	uint8_t		gek_key[G_ELI_DATAKEYLEN];
	/* Magic. */
	int		gek_magic;
	/* Key number. */
	uint64_t	gek_keyno;
	/* Reference counter. */
	int		gek_count;
	/* Keeps keys sorted by most recent use. */
	TAILQ_ENTRY(g_eli_key) gek_next;
	/* Keeps keys sorted by number. */
	RB_ENTRY(g_eli_key) gek_link;
};

struct g_eli_metadata {
	char		md_magic[16];	/* Magic value. */
	uint32_t	md_version;	/* Version number. */
	uint32_t	md_flags;	/* Additional flags. */
	uint16_t	md_ealgo;	/* Encryption algorithm. */
	uint16_t	md_keylen;	/* Key length. */
	uint16_t	md_aalgo;	/* Authentication algorithm. */
	uint64_t	md_provsize;	/* Provider's size. */
	uint32_t	md_sectorsize;	/* Sector size. */
	uint8_t		md_keys;	/* Available keys. */
	int32_t		md_iterations;	/* Number of iterations for PKCS#5v2. */
	uint8_t		md_salt[G_ELI_SALTLEN]; /* Salt. */
			/* Encrypted master key (IV-key, Data-key, HMAC). */
	uint8_t		md_mkeys[G_ELI_MAXMKEYS * G_ELI_MKEYLEN];
	u_char		md_hash[16];	/* MD5 hash. */
} __packed;
#ifndef _OpenSSL_
static __inline void
eli_metadata_encode_v0(struct g_eli_metadata *md, u_char **datap)
{
	u_char *p;

	p = *datap;
	le32enc(p, md->md_flags);	p += sizeof(md->md_flags);
	le16enc(p, md->md_ealgo);	p += sizeof(md->md_ealgo);
	le16enc(p, md->md_keylen);	p += sizeof(md->md_keylen);
	le64enc(p, md->md_provsize);	p += sizeof(md->md_provsize);
	le32enc(p, md->md_sectorsize);	p += sizeof(md->md_sectorsize);
	*p = md->md_keys;		p += sizeof(md->md_keys);
	le32enc(p, md->md_iterations);	p += sizeof(md->md_iterations);
	bcopy(md->md_salt, p, sizeof(md->md_salt)); p += sizeof(md->md_salt);
	bcopy(md->md_mkeys, p, sizeof(md->md_mkeys)); p += sizeof(md->md_mkeys);
	*datap = p;
}
static __inline void
eli_metadata_encode_v1v2v3v4v5v6v7(struct g_eli_metadata *md, u_char **datap)
{
	u_char *p;

	p = *datap;
	le32enc(p, md->md_flags);	p += sizeof(md->md_flags);
	le16enc(p, md->md_ealgo);	p += sizeof(md->md_ealgo);
	le16enc(p, md->md_keylen);	p += sizeof(md->md_keylen);
	le16enc(p, md->md_aalgo);	p += sizeof(md->md_aalgo);
	le64enc(p, md->md_provsize);	p += sizeof(md->md_provsize);
	le32enc(p, md->md_sectorsize);	p += sizeof(md->md_sectorsize);
	*p = md->md_keys;		p += sizeof(md->md_keys);
	le32enc(p, md->md_iterations);	p += sizeof(md->md_iterations);
	bcopy(md->md_salt, p, sizeof(md->md_salt)); p += sizeof(md->md_salt);
	bcopy(md->md_mkeys, p, sizeof(md->md_mkeys)); p += sizeof(md->md_mkeys);
	*datap = p;
}
static __inline void
eli_metadata_encode(struct g_eli_metadata *md, u_char *data)
{
	uint32_t hash[4];
	MD5_CTX ctx;
	u_char *p;

	p = data;
	bcopy(md->md_magic, p, sizeof(md->md_magic));
	p += sizeof(md->md_magic);
	le32enc(p, md->md_version);
	p += sizeof(md->md_version);
	switch (md->md_version) {
	case G_ELI_VERSION_00:
		eli_metadata_encode_v0(md, &p);
		break;
	case G_ELI_VERSION_01:
	case G_ELI_VERSION_02:
	case G_ELI_VERSION_03:
	case G_ELI_VERSION_04:
	case G_ELI_VERSION_05:
	case G_ELI_VERSION_06:
	case G_ELI_VERSION_07:
		eli_metadata_encode_v1v2v3v4v5v6v7(md, &p);
		break;
	default:
#ifdef _KERNEL
		panic("%s: Unsupported version %u.", __func__,
		    (u_int)md->md_version);
#else
		assert(!"Unsupported metadata version.");
#endif
	}
	MD5Init(&ctx);
	MD5Update(&ctx, data, p - data);
	MD5Final((void *)hash, &ctx);
	bcopy(hash, md->md_hash, sizeof(md->md_hash));
	bcopy(md->md_hash, p, sizeof(md->md_hash));
}
static __inline int
eli_metadata_decode_v0(const u_char *data, struct g_eli_metadata *md)
{
	uint32_t hash[4];
	MD5_CTX ctx;
	const u_char *p;

	p = data + sizeof(md->md_magic) + sizeof(md->md_version);
	md->md_flags = le32dec(p);	p += sizeof(md->md_flags);
	md->md_ealgo = le16dec(p);	p += sizeof(md->md_ealgo);
	md->md_keylen = le16dec(p);	p += sizeof(md->md_keylen);
	md->md_provsize = le64dec(p);	p += sizeof(md->md_provsize);
	md->md_sectorsize = le32dec(p);	p += sizeof(md->md_sectorsize);
	md->md_keys = *p;		p += sizeof(md->md_keys);
	md->md_iterations = le32dec(p);	p += sizeof(md->md_iterations);
	bcopy(p, md->md_salt, sizeof(md->md_salt)); p += sizeof(md->md_salt);
	bcopy(p, md->md_mkeys, sizeof(md->md_mkeys)); p += sizeof(md->md_mkeys);
	MD5Init(&ctx);
	MD5Update(&ctx, data, p - data);
	MD5Final((void *)hash, &ctx);
	bcopy(hash, md->md_hash, sizeof(md->md_hash));
	if (bcmp(md->md_hash, p, 16) != 0)
		return (EINVAL);
	return (0);
}

static __inline int
eli_metadata_decode_v1v2v3v4v5v6v7(const u_char *data, struct g_eli_metadata *md)
{
	uint32_t hash[4];
	MD5_CTX ctx;
	const u_char *p;

	p = data + sizeof(md->md_magic) + sizeof(md->md_version);
	md->md_flags = le32dec(p);	p += sizeof(md->md_flags);
	md->md_ealgo = le16dec(p);	p += sizeof(md->md_ealgo);
	md->md_keylen = le16dec(p);	p += sizeof(md->md_keylen);
	md->md_aalgo = le16dec(p);	p += sizeof(md->md_aalgo);
	md->md_provsize = le64dec(p);	p += sizeof(md->md_provsize);
	md->md_sectorsize = le32dec(p);	p += sizeof(md->md_sectorsize);
	md->md_keys = *p;		p += sizeof(md->md_keys);
	md->md_iterations = le32dec(p);	p += sizeof(md->md_iterations);
	bcopy(p, md->md_salt, sizeof(md->md_salt)); p += sizeof(md->md_salt);
	bcopy(p, md->md_mkeys, sizeof(md->md_mkeys)); p += sizeof(md->md_mkeys);
	MD5Init(&ctx);
	MD5Update(&ctx, data, p - data);
	MD5Final((void *)hash, &ctx);
	bcopy(hash, md->md_hash, sizeof(md->md_hash));
	if (bcmp(md->md_hash, p, 16) != 0)
		return (EINVAL);
	return (0);
}
static __inline int
eli_metadata_decode(const u_char *data, struct g_eli_metadata *md)
{
	int error;

	bcopy(data, md->md_magic, sizeof(md->md_magic));
	if (strcmp(md->md_magic, G_ELI_MAGIC) != 0)
		return (EINVAL);
	md->md_version = le32dec(data + sizeof(md->md_magic));
	switch (md->md_version) {
	case G_ELI_VERSION_00:
		error = eli_metadata_decode_v0(data, md);
		break;
	case G_ELI_VERSION_01:
	case G_ELI_VERSION_02:
	case G_ELI_VERSION_03:
	case G_ELI_VERSION_04:
	case G_ELI_VERSION_05:
	case G_ELI_VERSION_06:
	case G_ELI_VERSION_07:
		error = eli_metadata_decode_v1v2v3v4v5v6v7(data, md);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
#endif	/* !_OpenSSL */

static __inline u_int
g_eli_str2ealgo(const char *name)
{

	if (strcasecmp("null", name) == 0)
		return (CRYPTO_NULL_CBC);
	else if (strcasecmp("null-cbc", name) == 0)
		return (CRYPTO_NULL_CBC);
	else if (strcasecmp("aes", name) == 0)
		return (CRYPTO_AES_XTS);
	else if (strcasecmp("aes-cbc", name) == 0)
		return (CRYPTO_AES_CBC);
	else if (strcasecmp("aes-xts", name) == 0)
		return (CRYPTO_AES_XTS);
	else if (strcasecmp("blowfish", name) == 0)
		return (CRYPTO_BLF_CBC);
	else if (strcasecmp("blowfish-cbc", name) == 0)
		return (CRYPTO_BLF_CBC);
	else if (strcasecmp("camellia", name) == 0)
		return (CRYPTO_CAMELLIA_CBC);
	else if (strcasecmp("camellia-cbc", name) == 0)
		return (CRYPTO_CAMELLIA_CBC);
	else if (strcasecmp("3des", name) == 0)
		return (CRYPTO_3DES_CBC);
	else if (strcasecmp("3des-cbc", name) == 0)
		return (CRYPTO_3DES_CBC);
	return (CRYPTO_ALGORITHM_MIN - 1);
}

static __inline u_int
g_eli_str2aalgo(const char *name)
{

	if (strcasecmp("hmac/md5", name) == 0)
		return (CRYPTO_MD5_HMAC);
	else if (strcasecmp("hmac/sha1", name) == 0)
		return (CRYPTO_SHA1_HMAC);
	else if (strcasecmp("hmac/ripemd160", name) == 0)
		return (CRYPTO_RIPEMD160_HMAC);
	else if (strcasecmp("hmac/sha256", name) == 0)
		return (CRYPTO_SHA2_256_HMAC);
	else if (strcasecmp("hmac/sha384", name) == 0)
		return (CRYPTO_SHA2_384_HMAC);
	else if (strcasecmp("hmac/sha512", name) == 0)
		return (CRYPTO_SHA2_512_HMAC);
	return (CRYPTO_ALGORITHM_MIN - 1);
}

static __inline const char *
g_eli_algo2str(u_int algo)
{

	switch (algo) {
	case CRYPTO_NULL_CBC:
		return ("NULL");
	case CRYPTO_AES_CBC:
		return ("AES-CBC");
	case CRYPTO_AES_XTS:
		return ("AES-XTS");
	case CRYPTO_BLF_CBC:
		return ("Blowfish-CBC");
	case CRYPTO_CAMELLIA_CBC:
		return ("CAMELLIA-CBC");
	case CRYPTO_3DES_CBC:
		return ("3DES-CBC");
	case CRYPTO_MD5_HMAC:
		return ("HMAC/MD5");
	case CRYPTO_SHA1_HMAC:
		return ("HMAC/SHA1");
	case CRYPTO_RIPEMD160_HMAC:
		return ("HMAC/RIPEMD160");
	case CRYPTO_SHA2_256_HMAC:
		return ("HMAC/SHA256");
	case CRYPTO_SHA2_384_HMAC:
		return ("HMAC/SHA384");
	case CRYPTO_SHA2_512_HMAC:
		return ("HMAC/SHA512");
	}
	return ("unknown");
}

static __inline void
eli_metadata_dump(const struct g_eli_metadata *md)
{
	static const char hex[] = "0123456789abcdef";
	char str[sizeof(md->md_mkeys) * 2 + 1];
	u_int i;

	printf("     magic: %s\n", md->md_magic);
	printf("   version: %u\n", (u_int)md->md_version);
	printf("     flags: 0x%x\n", (u_int)md->md_flags);
	printf("     ealgo: %s\n", g_eli_algo2str(md->md_ealgo));
	printf("    keylen: %u\n", (u_int)md->md_keylen);
	if (md->md_flags & G_ELI_FLAG_AUTH)
		printf("     aalgo: %s\n", g_eli_algo2str(md->md_aalgo));
	printf("  provsize: %ju\n", (uintmax_t)md->md_provsize);
	printf("sectorsize: %u\n", (u_int)md->md_sectorsize);
	printf("      keys: 0x%02x\n", (u_int)md->md_keys);
	printf("iterations: %d\n", (int)md->md_iterations);
	bzero(str, sizeof(str));
	for (i = 0; i < sizeof(md->md_salt); i++) {
		str[i * 2] = hex[md->md_salt[i] >> 4];
		str[i * 2 + 1] = hex[md->md_salt[i] & 0x0f];
	}
	printf("      Salt: %s\n", str);
	bzero(str, sizeof(str));
	for (i = 0; i < sizeof(md->md_mkeys); i++) {
		str[i * 2] = hex[md->md_mkeys[i] >> 4];
		str[i * 2 + 1] = hex[md->md_mkeys[i] & 0x0f];
	}
	printf("Master Key: %s\n", str);
	bzero(str, sizeof(str));
	for (i = 0; i < 16; i++) {
		str[i * 2] = hex[md->md_hash[i] >> 4];
		str[i * 2 + 1] = hex[md->md_hash[i] & 0x0f];
	}
	printf("  MD5 hash: %s\n", str);
}

static __inline u_int
g_eli_keylen(u_int algo, u_int keylen)
{

	switch (algo) {
	case CRYPTO_NULL_CBC:
		if (keylen == 0)
			keylen = 64 * 8;
		else {
			if (keylen > 64 * 8)
				keylen = 0;
		}
		return (keylen);
	case CRYPTO_AES_CBC:
	case CRYPTO_CAMELLIA_CBC:
		switch (keylen) {
		case 0:
			return (128);
		case 128:
		case 192:
		case 256:
			return (keylen);
		default:
			return (0);
		}
	case CRYPTO_AES_XTS:
		switch (keylen) {
		case 0:
			return (128);
		case 128:
		case 256:
			return (keylen);
		default:
			return (0);
		}
	case CRYPTO_BLF_CBC:
		if (keylen == 0)
			return (128);
		if (keylen < 128 || keylen > 448)
			return (0);
		if ((keylen % 32) != 0)
			return (0);
		return (keylen);
	case CRYPTO_3DES_CBC:
		if (keylen == 0 || keylen == 192)
			return (192);
		return (0);
	default:
		return (0);
	}
}

static __inline u_int
g_eli_hashlen(u_int algo)
{

	switch (algo) {
	case CRYPTO_MD5_HMAC:
		return (16);
	case CRYPTO_SHA1_HMAC:
		return (20);
	case CRYPTO_RIPEMD160_HMAC:
		return (20);
	case CRYPTO_SHA2_256_HMAC:
		return (32);
	case CRYPTO_SHA2_384_HMAC:
		return (48);
	case CRYPTO_SHA2_512_HMAC:
		return (64);
	}
	return (0);
}

static __inline void
eli_metadata_softc(struct g_eli_softc *sc, const struct g_eli_metadata *md,
    u_int sectorsize, off_t mediasize)
{

	sc->sc_version = md->md_version;
	sc->sc_inflight = 0;
	sc->sc_crypto = G_ELI_CRYPTO_UNKNOWN;
	sc->sc_flags = md->md_flags;
	/* Backward compatibility. */
	if (md->md_version < G_ELI_VERSION_04)
		sc->sc_flags |= G_ELI_FLAG_NATIVE_BYTE_ORDER;
	if (md->md_version < G_ELI_VERSION_05)
		sc->sc_flags |= G_ELI_FLAG_SINGLE_KEY;
	if (md->md_version < G_ELI_VERSION_06 &&
	    (sc->sc_flags & G_ELI_FLAG_AUTH) != 0) {
		sc->sc_flags |= G_ELI_FLAG_FIRST_KEY;
	}
	if (md->md_version < G_ELI_VERSION_07)
		sc->sc_flags |= G_ELI_FLAG_ENC_IVKEY;
	sc->sc_ealgo = md->md_ealgo;

	if (sc->sc_flags & G_ELI_FLAG_AUTH) {
		sc->sc_akeylen = sizeof(sc->sc_akey) * 8;
		sc->sc_aalgo = md->md_aalgo;
		sc->sc_alen = g_eli_hashlen(sc->sc_aalgo);

		sc->sc_data_per_sector = sectorsize - sc->sc_alen;
		/*
		 * Some hash functions (like SHA1 and RIPEMD160) generates hash
		 * which length is not multiple of 128 bits, but we want data
		 * length to be multiple of 128, so we can encrypt without
		 * padding. The line below rounds down data length to multiple
		 * of 128 bits.
		 */
		sc->sc_data_per_sector -= sc->sc_data_per_sector % 16;

		sc->sc_bytes_per_sector =
		    (md->md_sectorsize - 1) / sc->sc_data_per_sector + 1;
		sc->sc_bytes_per_sector *= sectorsize;
	}
	sc->sc_sectorsize = md->md_sectorsize;
	sc->sc_mediasize = mediasize;
	if (!(sc->sc_flags & G_ELI_FLAG_ONETIME))
		sc->sc_mediasize -= sectorsize;
	if (!(sc->sc_flags & G_ELI_FLAG_AUTH))
		sc->sc_mediasize -= (sc->sc_mediasize % sc->sc_sectorsize);
	else {
		sc->sc_mediasize /= sc->sc_bytes_per_sector;
		sc->sc_mediasize *= sc->sc_sectorsize;
	}
	sc->sc_ekeylen = md->md_keylen;
}

#ifdef _KERNEL
int g_eli_read_metadata(struct g_class *mp, struct g_provider *pp,
    struct g_eli_metadata *md);
struct g_geom *g_eli_create(struct gctl_req *req, struct g_class *mp,
    struct g_provider *bpp, const struct g_eli_metadata *md,
    const u_char *mkey, int nkey);
int g_eli_destroy(struct g_eli_softc *sc, boolean_t force);

int g_eli_access(struct g_provider *pp, int dr, int dw, int de);
void g_eli_config(struct gctl_req *req, struct g_class *mp, const char *verb);

void g_eli_read_done(struct bio *bp);
void g_eli_write_done(struct bio *bp);
int g_eli_crypto_rerun(struct cryptop *crp);

void g_eli_crypto_read(struct g_eli_softc *sc, struct bio *bp, boolean_t fromworker);
void g_eli_crypto_run(struct g_eli_worker *wr, struct bio *bp);

void g_eli_auth_read(struct g_eli_softc *sc, struct bio *bp);
void g_eli_auth_run(struct g_eli_worker *wr, struct bio *bp);
#endif
void g_eli_crypto_ivgen(struct g_eli_softc *sc, off_t offset, u_char *iv,
    size_t size);

void g_eli_mkey_hmac(unsigned char *mkey, const unsigned char *key);
int g_eli_mkey_decrypt(const struct g_eli_metadata *md,
    const unsigned char *key, unsigned char *mkey, unsigned keyp);
int g_eli_mkey_decrypt_any(const struct g_eli_metadata *md,
    const unsigned char *key, unsigned char *mkey, unsigned *nkeyp);
int g_eli_mkey_encrypt(unsigned algo, const unsigned char *key, unsigned keylen,
    unsigned char *mkey);
#ifdef _KERNEL
void g_eli_mkey_propagate(struct g_eli_softc *sc, const unsigned char *mkey);
#endif

int g_eli_crypto_encrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize);
int g_eli_crypto_decrypt(u_int algo, u_char *data, size_t datasize,
    const u_char *key, size_t keysize);

struct hmac_ctx {
	SHA512_CTX	innerctx;
	SHA512_CTX	outerctx;
};

void g_eli_crypto_hmac_init(struct hmac_ctx *ctx, const uint8_t *hkey,
    size_t hkeylen);
void g_eli_crypto_hmac_update(struct hmac_ctx *ctx, const uint8_t *data,
    size_t datasize);
void g_eli_crypto_hmac_final(struct hmac_ctx *ctx, uint8_t *md, size_t mdsize);
void g_eli_crypto_hmac(const uint8_t *hkey, size_t hkeysize,
    const uint8_t *data, size_t datasize, uint8_t *md, size_t mdsize);

void g_eli_key_fill(struct g_eli_softc *sc, struct g_eli_key *key,
    uint64_t keyno);
#ifdef _KERNEL
void g_eli_key_init(struct g_eli_softc *sc);
void g_eli_key_destroy(struct g_eli_softc *sc);
uint8_t *g_eli_key_hold(struct g_eli_softc *sc, off_t offset, size_t blocksize);
void g_eli_key_drop(struct g_eli_softc *sc, uint8_t *rawkey);
#endif
#endif	/* !_G_ELI_H_ */
