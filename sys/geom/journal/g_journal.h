/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#ifndef	_G_JOURNAL_H_
#define	_G_JOURNAL_H_

#include <sys/endian.h>
#include <sys/md5.h>
#ifdef _KERNEL
#include <sys/bio.h>
#endif

#define	G_JOURNAL_CLASS_NAME	"JOURNAL"

#define	G_JOURNAL_MAGIC		"GEOM::JOURNAL"
/*
 * Version history:
 * 0 - Initial version number.
 */
#define	G_JOURNAL_VERSION	0

#ifdef _KERNEL
extern int g_journal_debug;

#define	GJ_DEBUG(lvl, ...)	do {					\
	if (g_journal_debug >= (lvl)) {					\
		printf("GEOM_JOURNAL");					\
		if (g_journal_debug > 0)				\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)
#define	GJ_LOGREQ(lvl, bp, ...)	do {					\
	if (g_journal_debug >= (lvl)) {					\
		printf("GEOM_JOURNAL");					\
		if (g_journal_debug > 0)				\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

#define	JEMPTY(sc)	((sc)->sc_journal_offset -			\
			 (sc)->sc_jprovider->sectorsize ==		\
			 (sc)->sc_active.jj_offset &&			\
			 (sc)->sc_current_count == 0)

#define	GJ_BIO_REGULAR		0x00
#define	GJ_BIO_READ		0x01
#define	GJ_BIO_JOURNAL		0x02
#define	GJ_BIO_COPY		0x03
#define	GJ_BIO_MASK		0x0f

#if 0
#define	GJF_BIO_DONT_FREE	0x10
#define	GJF_BIO_MASK		0xf0
#endif

#define	GJF_DEVICE_HARDCODED		0x0001
#define	GJF_DEVICE_DESTROY		0x0010
#define	GJF_DEVICE_SWITCH		0x0020
#define	GJF_DEVICE_BEFORE_SWITCH	0x0040
#define	GJF_DEVICE_CLEAN		0x0080
#define	GJF_DEVICE_CHECKSUM		0x0100

#define	GJ_HARD_LIMIT		64

/*
 * We keep pointers to journaled data in bio structure and because we
 * need to store two off_t values (offset in data provider and offset in
 * journal), we have to borrow bio_completed field for this.
 */
#define	bio_joffset	bio_completed
/*
 * Use bio_caller1 field as a pointer in queue.
 */
#define	bio_next	bio_caller1

/*
 * There are two such structures maintained inside each journaled device.
 * One describes active part of the journal, were recent requests are stored.
 * The second describes the last consistent part of the journal with requests
 * that are copied to the destination provider.
 */
struct g_journal_journal {
	struct bio	*jj_queue;	/* Cached journal entries. */
	off_t		 jj_offset;	/* Journal's start offset. */
};

struct g_journal_softc {
	uint32_t	 sc_id;
	uint8_t		 sc_type;
	uint8_t		 sc_orig_type;
	struct g_geom	*sc_geom;
	u_int		 sc_flags;
	struct mtx	 sc_mtx;
	off_t		 sc_mediasize;
	u_int		 sc_sectorsize;
#define	GJ_FLUSH_DATA		0x01
#define	GJ_FLUSH_JOURNAL	0x02
	u_int		 sc_bio_flush;

	uint32_t	 sc_journal_id;
	uint32_t	 sc_journal_next_id;
	int		 sc_journal_copying;
	off_t		 sc_journal_offset;
	off_t		 sc_journal_previous_id;

	struct bio_queue_head sc_back_queue;
	struct bio_queue_head sc_regular_queue;

	struct bio_queue_head sc_delayed_queue;
	int		 sc_delayed_count;

	struct bio	*sc_current_queue;
	int		 sc_current_count;

	struct bio	*sc_flush_queue;
	int		 sc_flush_count;
	int		 sc_flush_in_progress;

	struct bio	*sc_copy_queue;
	int		 sc_copy_in_progress;

	struct g_consumer *sc_dconsumer;
	struct g_consumer *sc_jconsumer;

	struct g_journal_journal sc_inactive;
	struct g_journal_journal sc_active;

	off_t		 sc_jstart;	/* Journal space start offset. */
	off_t		 sc_jend;	/* Journal space end offset. */

	struct callout	 sc_callout;
	struct proc	*sc_worker;

	struct root_hold_token *sc_rootmount;
};
#define	sc_dprovider	sc_dconsumer->provider
#define	sc_jprovider	sc_jconsumer->provider
#define	sc_name		sc_dprovider->name

#define	GJQ_INSERT_HEAD(head, bp)	do {				\
	(bp)->bio_next = (head);					\
	(head) = (bp);							\
} while (0)
#define	GJQ_INSERT_AFTER(head, bp, pbp)	do {				\
	if ((pbp) == NULL)						\
		GJQ_INSERT_HEAD(head, bp);				\
	else {								\
		(bp)->bio_next = (pbp)->bio_next;			\
		(pbp)->bio_next = (bp);					\
	}								\
} while (0)
#define GJQ_LAST(head, bp) do {						\
	struct bio *_bp;						\
									\
	if ((head) == NULL) {						\
		(bp) = (head);						\
		break;							\
	}								\
	for (_bp = (head); _bp->bio_next != NULL; _bp = _bp->bio_next)	\
		continue;						\
	(bp) = (_bp);							\
} while (0)
#define	GJQ_FIRST(head)	(head)
#define	GJQ_REMOVE(head, bp)	do {					\
	struct bio *_bp;						\
									\
	if ((head) == (bp)) {						\
		(head) = (bp)->bio_next;				\
		(bp)->bio_next = NULL;					\
		break;							\
	}								\
	for (_bp = (head); _bp->bio_next != NULL; _bp = _bp->bio_next) {\
		if (_bp->bio_next == (bp))				\
			break;						\
	}								\
	KASSERT(_bp->bio_next != NULL, ("NULL bio_next"));		\
	KASSERT(_bp->bio_next == (bp), ("bio_next != bp"));		\
	_bp->bio_next = (bp)->bio_next;					\
	(bp)->bio_next = NULL;						\
} while (0)
#define GJQ_FOREACH(head, bp)						\
	for ((bp) = (head); (bp) != NULL; (bp) = (bp)->bio_next)

#define	GJ_HEADER_MAGIC	"GJHDR"

struct g_journal_header {
	char		jh_magic[sizeof(GJ_HEADER_MAGIC)];
	uint32_t	jh_journal_id;
	uint32_t	jh_journal_next_id;
} __packed;

struct g_journal_entry {
	uint64_t	je_joffset;
	uint64_t	je_offset;
	uint64_t	je_length;
} __packed;

#define	GJ_RECORD_HEADER_MAGIC		"GJRHDR"
#define	GJ_RECORD_HEADER_NENTRIES	(20)
#define	GJ_RECORD_MAX_SIZE(sc)	\
	((sc)->sc_jprovider->sectorsize + GJ_RECORD_HEADER_NENTRIES * MAXPHYS)
#define	GJ_VALIDATE_OFFSET(offset, sc)	do {				\
	if ((offset) + GJ_RECORD_MAX_SIZE(sc) >= (sc)->sc_jend) {	\
		(offset) = (sc)->sc_jstart;				\
		GJ_DEBUG(2, "Starting from the beginning (%s).",		\
		    (sc)->sc_name);					\
	}								\
} while (0)

struct g_journal_record_header {
	char		jrh_magic[sizeof(GJ_RECORD_HEADER_MAGIC)];
	uint32_t	jrh_journal_id;
	uint16_t	jrh_nentries;
	u_char		jrh_sum[8];
	struct g_journal_entry jrh_entries[GJ_RECORD_HEADER_NENTRIES];
} __packed;

typedef int (g_journal_clean_t)(struct mount *mp);
typedef void (g_journal_dirty_t)(struct g_consumer *cp);

struct g_journal_desc {
	const char		*jd_fstype;
	g_journal_clean_t	*jd_clean;
	g_journal_dirty_t	*jd_dirty;
};

/* Supported file systems. */
extern const struct g_journal_desc g_journal_ufs;

#define	GJ_TIMER_START(lvl, bt)	do {					\
	if (g_journal_debug >= (lvl))					\
		binuptime(bt);						\
} while (0)
#define	GJ_TIMER_STOP(lvl, bt, ...)	do {				\
	if (g_journal_debug >= (lvl)) {					\
		struct bintime _bt2;					\
		struct timeval _tv;					\
									\
		binuptime(&_bt2);					\
		bintime_sub(&_bt2, bt);					\
		bintime2timeval(&_bt2, &_tv);				\
		printf("GEOM_JOURNAL");					\
		if (g_journal_debug > 0)				\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf(": %jd.%06jds\n", (intmax_t)_tv.tv_sec,		\
		    (intmax_t)_tv.tv_usec);				\
	}								\
} while (0)
#endif	/* _KERNEL */

#define	GJ_TYPE_DATA		0x01
#define	GJ_TYPE_JOURNAL		0x02
#define	GJ_TYPE_COMPLETE	(GJ_TYPE_DATA|GJ_TYPE_JOURNAL)

#define	GJ_FLAG_CLEAN		0x01
#define	GJ_FLAG_CHECKSUM	0x02

struct g_journal_metadata {
	char		md_magic[16];	/* Magic value. */
	uint32_t	md_version;	/* Version number. */
	uint32_t	md_id;		/* Journal unique ID. */
	uint8_t		md_type;	/* Provider type. */
	uint64_t	md_jstart;	/* Journal space start offset. */
	uint64_t	md_jend;	/* Journal space end offset. */
	uint64_t	md_joffset;	/* Last known consistent journal offset. */
	uint32_t	md_jid;		/* Last known consistent journal ID. */
	uint64_t	md_flags;	/* Journal flags. */
	char		md_provider[16]; /* Hardcoded provider. */
	uint64_t	md_provsize;	/* Provider's size. */
	u_char		md_hash[16];	/* MD5 hash. */
};
static __inline void
journal_metadata_encode(struct g_journal_metadata *md, u_char *data)
{
	MD5_CTX ctx;

	bcopy(md->md_magic, data, 16);
	le32enc(data + 16, md->md_version);
	le32enc(data + 20, md->md_id);
	*(data + 24) = md->md_type;
	le64enc(data + 25, md->md_jstart);
	le64enc(data + 33, md->md_jend);
	le64enc(data + 41, md->md_joffset);
	le32enc(data + 49, md->md_jid);
	le64enc(data + 53, md->md_flags);
	bcopy(md->md_provider, data + 61, 16);
	le64enc(data + 77, md->md_provsize);
	MD5Init(&ctx);
	MD5Update(&ctx, data, 85);
	MD5Final(md->md_hash, &ctx);
	bcopy(md->md_hash, data + 85, 16);
}
static __inline int
journal_metadata_decode_v0(const u_char *data, struct g_journal_metadata *md)
{
	MD5_CTX ctx;

	md->md_id = le32dec(data + 20);
	md->md_type = *(data + 24);
	md->md_jstart = le64dec(data + 25);
	md->md_jend = le64dec(data + 33);
	md->md_joffset = le64dec(data + 41);
	md->md_jid = le32dec(data + 49);
	md->md_flags = le64dec(data + 53);
	bcopy(data + 61, md->md_provider, 16);
	md->md_provsize = le64dec(data + 77);
	MD5Init(&ctx);
	MD5Update(&ctx, data, 85);
	MD5Final(md->md_hash, &ctx);
	if (bcmp(md->md_hash, data + 85, 16) != 0)
		return (EINVAL);
	return (0);
}
static __inline int
journal_metadata_decode(const u_char *data, struct g_journal_metadata *md)
{
	int error;

	bcopy(data, md->md_magic, 16);
	md->md_version = le32dec(data + 16);
	switch (md->md_version) {
	case 0:
		error = journal_metadata_decode_v0(data, md);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static __inline void
journal_metadata_dump(const struct g_journal_metadata *md)
{
	static const char hex[] = "0123456789abcdef";
	char hash[16 * 2 + 1];
	u_int i;

	printf("     magic: %s\n", md->md_magic);
	printf("   version: %u\n", (u_int)md->md_version);
	printf("        id: %u\n", (u_int)md->md_id);
	printf("      type: %u\n", (u_int)md->md_type);
	printf("     start: %ju\n", (uintmax_t)md->md_jstart);
	printf("       end: %ju\n", (uintmax_t)md->md_jend);
	printf("   joffset: %ju\n", (uintmax_t)md->md_joffset);
	printf("       jid: %u\n", (u_int)md->md_jid);
	printf("     flags: %u\n", (u_int)md->md_flags);
	printf("hcprovider: %s\n", md->md_provider);
	printf("  provsize: %ju\n", (uintmax_t)md->md_provsize);
	bzero(hash, sizeof(hash));
	for (i = 0; i < 16; i++) {
		hash[i * 2] = hex[md->md_hash[i] >> 4];
		hash[i * 2 + 1] = hex[md->md_hash[i] & 0x0f];
	}
	printf("  MD5 hash: %s\n", hash);
}
#endif	/* !_G_JOURNAL_H_ */
