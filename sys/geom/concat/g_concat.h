/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#ifndef	_G_CONCAT_H_
#define	_G_CONCAT_H_

#include <sys/endian.h>

#define	G_CONCAT_CLASS_NAME	"CONCAT"

#define	G_CONCAT_MAGIC		"GEOM::CONCAT"
/*
 * Version history:
 * 1 - Initial version number.
 * 2 - Added 'stop' command to gconcat(8).
 * 3 - Added md_provider field to metadata and '-h' option to gconcat(8).
 * 4 - Added md_provsize field to metadata.
 */
#define	G_CONCAT_VERSION	4

#ifdef _KERNEL
#define	G_CONCAT_TYPE_MANUAL	0
#define	G_CONCAT_TYPE_AUTOMATIC	1

#define	G_CONCAT_DEBUG(lvl, ...)	do {				\
	if (g_concat_debug >= (lvl)) {					\
		printf("GEOM_CONCAT");					\
		if (g_concat_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)
#define	G_CONCAT_LOGREQ(bp, ...)	do {				\
	if (g_concat_debug >= 2) {					\
		printf("GEOM_CONCAT[2]: ");				\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

struct g_concat_disk {
	struct g_consumer	*d_consumer;
	struct g_concat_softc	*d_softc;
	off_t			 d_start;
	off_t			 d_end;
	int			 d_candelete;
	int			 d_removed;
};

struct g_concat_softc {
	u_int		 sc_type;	/* provider type */
	struct g_geom	*sc_geom;
	struct g_provider *sc_provider;
	uint32_t	 sc_id;		/* concat unique ID */

	struct g_concat_disk *sc_disks;
	uint16_t	 sc_ndisks;
	struct mtx	 sc_lock;
};
#define	sc_name	sc_geom->name
#endif	/* _KERNEL */

struct g_concat_metadata {
	char		md_magic[16];	/* Magic value. */
	uint32_t	md_version;	/* Version number. */
	char		md_name[16];	/* Concat name. */
	uint32_t	md_id;		/* Unique ID. */
	uint16_t	md_no;		/* Disk number. */
	uint16_t	md_all;		/* Number of all disks. */
	char		md_provider[16]; /* Hardcoded provider. */
	uint64_t	md_provsize;	/* Provider's size. */
};
static __inline void
concat_metadata_encode(const struct g_concat_metadata *md, u_char *data)
{

	bcopy(md->md_magic, data, sizeof(md->md_magic));
	le32enc(data + 16, md->md_version);
	bcopy(md->md_name, data + 20, sizeof(md->md_name));
	le32enc(data + 36, md->md_id);
	le16enc(data + 40, md->md_no);
	le16enc(data + 42, md->md_all);
	bcopy(md->md_provider, data + 44, sizeof(md->md_provider));
	le64enc(data + 60, md->md_provsize);
}
static __inline void
concat_metadata_decode(const u_char *data, struct g_concat_metadata *md)
{

	bcopy(data, md->md_magic, sizeof(md->md_magic));
	md->md_version = le32dec(data + 16);
	bcopy(data + 20, md->md_name, sizeof(md->md_name));
	md->md_id = le32dec(data + 36);
	md->md_no = le16dec(data + 40);
	md->md_all = le16dec(data + 42);
	bcopy(data + 44, md->md_provider, sizeof(md->md_provider));
	md->md_provsize = le64dec(data + 60);
}
#endif	/* _G_CONCAT_H_ */
