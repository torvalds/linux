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

#ifndef	_G_LABEL_H_
#define	_G_LABEL_H_

#include <sys/endian.h>
#ifdef _KERNEL
#include <sys/sysctl.h>
#endif

#define	G_LABEL_CLASS_NAME	"LABEL"

#define	G_LABEL_MAGIC		"GEOM::LABEL"
/*
 * Version history:
 * 1 - Initial version number.
 * 2 - Added md_provsize field to metadata.
 */
#define	G_LABEL_VERSION		2
#define	G_LABEL_DIR		"label"

#ifdef _KERNEL
extern u_int g_label_debug;

#define	G_LABEL_DEBUG(lvl, ...)	do {					\
	if (g_label_debug >= (lvl)) {					\
		printf("GEOM_LABEL");					\
		if (g_label_debug > 0)					\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

SYSCTL_DECL(_kern_geom_label);

#define	G_LABEL_INIT(kind, label, descr) 				\
	SYSCTL_NODE(_kern_geom_label, OID_AUTO, kind, CTLFLAG_RD,	\
	    NULL, "");							\
	SYSCTL_INT(_kern_geom_label_##kind, OID_AUTO, enable, 		\
	    CTLFLAG_RWTUN, &label.ld_enabled, 1, descr)

typedef void g_label_taste_t (struct g_consumer *cp, char *label, size_t size);

struct g_label_desc {
	g_label_taste_t	*ld_taste;
	char		*ld_dir;
	int		 ld_enabled;
};

/* Supported labels. */
extern struct g_label_desc g_label_ufs_id;
extern struct g_label_desc g_label_ufs_volume;
extern struct g_label_desc g_label_iso9660;
extern struct g_label_desc g_label_msdosfs;
extern struct g_label_desc g_label_ext2fs;
extern struct g_label_desc g_label_reiserfs;
extern struct g_label_desc g_label_ntfs;
extern struct g_label_desc g_label_gpt;
extern struct g_label_desc g_label_gpt_uuid;
extern struct g_label_desc g_label_disk_ident;
extern struct g_label_desc g_label_flashmap;

extern void g_label_rtrim(char *label, size_t size);
#endif	/* _KERNEL */

struct g_label_metadata {
	char		md_magic[16];	/* Magic value. */
	uint32_t	md_version;	/* Version number. */
	char		md_label[16];	/* Label. */
	uint64_t	md_provsize;	/* Provider's size. */
};
static __inline void
label_metadata_encode(const struct g_label_metadata *md, u_char *data)
{

	bcopy(md->md_magic, data, sizeof(md->md_magic));
	le32enc(data + 16, md->md_version);
	bcopy(md->md_label, data + 20, sizeof(md->md_label));
	le64enc(data + 36, md->md_provsize);
}
static __inline void
label_metadata_decode(const u_char *data, struct g_label_metadata *md)
{

	bcopy(data, md->md_magic, sizeof(md->md_magic));
	md->md_version = le32dec(data + 16);
	bcopy(data + 20, md->md_label, sizeof(md->md_label));
	md->md_provsize = le64dec(data + 36);
}
#endif	/* _G_LABEL_H_ */
