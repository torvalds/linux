/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Ruslan Ermilov <ru@FreeBSD.org>
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

#ifndef	_G_CACHE_H_
#define	_G_CACHE_H_

#include <sys/endian.h>

#define	G_CACHE_CLASS_NAME	"CACHE"
#define	G_CACHE_MAGIC		"GEOM::CACHE"
#define	G_CACHE_VERSION		1

#ifdef _KERNEL
#define	G_CACHE_TYPE_MANUAL	0
#define	G_CACHE_TYPE_AUTOMATIC	1

#define	G_CACHE_DEBUG(lvl, ...)	do {					\
	if (g_cache_debug >= (lvl)) {					\
		printf("GEOM_CACHE");					\
		if (g_cache_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)
#define	G_CACHE_LOGREQ(bp, ...)	do {					\
	if (g_cache_debug >= 2) {					\
		printf("GEOM_CACHE[2]: ");				\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

#define	G_CACHE_BUCKETS		(1 << 3)
#define	G_CACHE_BUCKET(bno)	((bno) & (G_CACHE_BUCKETS - 1))

struct g_cache_softc {
	struct g_geom	*sc_geom;
	int		sc_type;
	u_int		sc_bshift;
	u_int		sc_bsize;
	off_t		sc_tail;
	struct mtx	sc_mtx;
	struct callout	sc_callout;
	LIST_HEAD(, g_cache_desc) sc_desclist[G_CACHE_BUCKETS];
	TAILQ_HEAD(, g_cache_desc) sc_usedlist;
	uma_zone_t	sc_zone;

	u_int		sc_maxent;		/* max entries */
	u_int		sc_nent;		/* allocated entries */
	u_int		sc_nused;		/* re-useable entries */
	u_int		sc_invalid;		/* invalid entries */

	uintmax_t	sc_reads;		/* #reads */
	uintmax_t	sc_readbytes;		/* bytes read */
	uintmax_t	sc_cachereads;		/* #reads from cache */
	uintmax_t	sc_cachereadbytes;	/* bytes read from cache */
	uintmax_t	sc_cachehits;		/* cache hits */
	uintmax_t	sc_cachemisses;		/* cache misses */
	uintmax_t	sc_cachefull;		/* #times a cache was full */
	uintmax_t	sc_writes;		/* #writes */
	uintmax_t	sc_wrotebytes;		/* bytes written */
};
#define	sc_name	sc_geom->name

struct g_cache_desc {
	off_t		d_bno;			/* block number */
	caddr_t		d_data;			/* data area */
	struct bio	*d_biolist;		/* waiters */
	time_t		d_atime;		/* access time */
	int		d_flags;		/* flags */
#define	D_FLAG_USED	(1 << 0)			/* can be reused */
#define	D_FLAG_INVALID	(1 << 1)			/* invalid */
	LIST_ENTRY(g_cache_desc) d_next;	/* list */
	TAILQ_ENTRY(g_cache_desc) d_used;	/* used list */
};

#define	G_CACHE_NEXT_BIO1(bp)	(bp)->bio_driver1
#define	G_CACHE_NEXT_BIO2(bp)	(bp)->bio_driver2
#define	G_CACHE_DESC1(bp)	(bp)->bio_caller1
#define	G_CACHE_DESC2(bp)	(bp)->bio_caller2

#endif	/* _KERNEL */

struct g_cache_metadata {
	char		md_magic[16];		/* Magic value. */
	uint32_t	md_version;		/* Version number. */
	char		md_name[16];		/* Cache value. */
	uint32_t	md_bsize;		/* Cache block size. */
	uint32_t	md_size;		/* Cache size. */
	uint64_t	md_provsize;		/* Provider's size. */
};

static __inline void
cache_metadata_encode(const struct g_cache_metadata *md, u_char *data)
{

	bcopy(md->md_magic, data, sizeof(md->md_magic));
	le32enc(data + 16, md->md_version);
	bcopy(md->md_name, data + 20, sizeof(md->md_name));
	le32enc(data + 36, md->md_bsize);
	le32enc(data + 40, md->md_size);
	le64enc(data + 44, md->md_provsize);
}

static __inline void
cache_metadata_decode(const u_char *data, struct g_cache_metadata *md)
{

	bcopy(data, md->md_magic, sizeof(md->md_magic));
	md->md_version = le32dec(data + 16);
	bcopy(data + 20, md->md_name, sizeof(md->md_name));
	md->md_bsize = le32dec(data + 36);
	md->md_size = le32dec(data + 40);
	md->md_provsize = le64dec(data + 44);
}

#endif	/* _G_CACHE_H_ */
