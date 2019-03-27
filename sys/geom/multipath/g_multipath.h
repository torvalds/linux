/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Matthew Jacob <mjacob@FreeBSD.org>
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
/*
 * Based upon work by Pawel Jakub Dawidek <pjd@FreeBSD.org> for all of the
 * fine geom examples, and by Poul Henning Kamp <phk@FreeBSD.org> for GEOM
 * itself, all of which is most gratefully acknowledged.
 */

#ifndef	_G_MULTIPATH_H_
#define	_G_MULTIPATH_H_

#define	G_MULTIPATH_CLASS_NAME	"MULTIPATH"
#define	G_MULTIPATH_VERSION	1
#define	G_MULTIPATH_MAGIC	"GEOM::MULTIPATH"

#include <sys/endian.h>

#ifdef	_KERNEL

struct g_multipath_softc {
	struct g_provider *	sc_pp;
	struct g_consumer *	sc_active;
	struct mtx		sc_mtx;
	char			sc_name[16];
	char			sc_uuid[40];
	off_t			sc_size;
	int			sc_opened;
	int			sc_stopping;
	int			sc_ndisks;
	int			sc_active_active; /* Active/Active mode */
};
#endif	/* _KERNEL */

struct g_multipath_metadata {
	char		md_magic[16];	/* Magic Value */
	char 		md_uuid[40];	/* more magic */
	char		md_name[16];	/* a friendly name */
	uint32_t	md_version;	/* version */
	uint32_t	md_sectorsize;	/* sectorsize of provider */
	uint64_t	md_size;	/* absolute size of provider */
	uint8_t		md_active_active; /* Active/Active mode */
};

static __inline void
multipath_metadata_encode(const struct g_multipath_metadata *, u_char *);

static __inline void
multipath_metadata_decode(u_char *, struct g_multipath_metadata *);

static __inline void
multipath_metadata_encode(const struct g_multipath_metadata *md, u_char *data)
{
	bcopy(md->md_magic, data, sizeof(md->md_magic));
	data += sizeof(md->md_magic);
	bcopy(md->md_uuid, data, sizeof(md->md_uuid));
	data += sizeof(md->md_uuid);
	bcopy(md->md_name, data, sizeof(md->md_name));
	data += sizeof(md->md_name);
	le32enc(data, md->md_version);
	data += sizeof(md->md_version);
	le32enc(data, md->md_sectorsize);
	data += sizeof(md->md_sectorsize);
	le64enc(data, md->md_size);
	data += sizeof(md->md_size);
	*data = md->md_active_active;
}

static __inline void
multipath_metadata_decode(u_char *data, struct g_multipath_metadata *md)
{
	bcopy(data, md->md_magic, sizeof(md->md_magic));
	data += sizeof(md->md_magic);
	bcopy(data, md->md_uuid, sizeof(md->md_uuid));
	data += sizeof(md->md_uuid);
	bcopy(data, md->md_name, sizeof(md->md_name));
	data += sizeof(md->md_name);
	md->md_version = le32dec(data);
	data += sizeof(md->md_version);
	md->md_sectorsize = le32dec(data);
	data += sizeof(md->md_sectorsize);
	md->md_size = le64dec(data);
	data += sizeof(md->md_size);
	md->md_active_active = *data;
}
#endif	/* _G_MULTIPATH_H_ */
