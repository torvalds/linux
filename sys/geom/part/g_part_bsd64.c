/*-
 * Copyright (c) 2014 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/disklabel.h>
#include <sys/endian.h>
#include <sys/gpt.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_bsd64, "GEOM partitioning class for 64-bit BSD disklabels");

/* XXX: move this to sys/disklabel64.h */
#define	DISKMAGIC64     ((uint32_t)0xc4464c59)
#define	MAXPARTITIONS64	16
#define	RESPARTITIONS64	32

struct disklabel64 {
	char	  d_reserved0[512];	/* reserved or unused */
	u_int32_t d_magic;		/* the magic number */
	u_int32_t d_crc;		/* crc32() d_magic through last part */
	u_int32_t d_align;		/* partition alignment requirement */
	u_int32_t d_npartitions;	/* number of partitions */
	struct uuid d_stor_uuid;	/* unique uuid for label */

	u_int64_t d_total_size;		/* total size incl everything (bytes) */
	u_int64_t d_bbase;		/* boot area base offset (bytes) */
					/* boot area is pbase - bbase */
	u_int64_t d_pbase;		/* first allocatable offset (bytes) */
	u_int64_t d_pstop;		/* last allocatable offset+1 (bytes) */
	u_int64_t d_abase;		/* location of backup copy if not 0 */

	u_char	  d_packname[64];
	u_char    d_reserved[64];

	/*
	 * Note: offsets are relative to the base of the slice, NOT to
	 * d_pbase.  Unlike 32 bit disklabels the on-disk format for
	 * a 64 bit disklabel remains slice-relative.
	 *
	 * An uninitialized partition has a p_boffset and p_bsize of 0.
	 *
	 * If p_fstype is not supported for a live partition it is set
	 * to FS_OTHER.  This is typically the case when the filesystem
	 * is identified by its uuid.
	 */
	struct partition64 {		/* the partition table */
		u_int64_t p_boffset;	/* slice relative offset, in bytes */
		u_int64_t p_bsize;	/* size of partition, in bytes */
		u_int8_t  p_fstype;
		u_int8_t  p_unused01;	/* reserved, must be 0 */
		u_int8_t  p_unused02;	/* reserved, must be 0 */
		u_int8_t  p_unused03;	/* reserved, must be 0 */
		u_int32_t p_unused04;	/* reserved, must be 0 */
		u_int32_t p_unused05;	/* reserved, must be 0 */
		u_int32_t p_unused06;	/* reserved, must be 0 */
		struct uuid p_type_uuid;/* mount type as UUID */
		struct uuid p_stor_uuid;/* unique uuid for storage */
	} d_partitions[MAXPARTITIONS64];/* actually may be more */
};

struct g_part_bsd64_table {
	struct g_part_table	base;

	uint32_t		d_align;
	uint64_t		d_bbase;
	uint64_t		d_abase;
	struct uuid		d_stor_uuid;
	char			d_reserved0[512];
	u_char			d_packname[64];
	u_char			d_reserved[64];
};

struct g_part_bsd64_entry {
	struct g_part_entry	base;

	uint8_t			fstype;
	struct uuid		type_uuid;
	struct uuid		stor_uuid;
};

static int g_part_bsd64_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_bsd64_bootcode(struct g_part_table *, struct g_part_parms *);
static int g_part_bsd64_create(struct g_part_table *, struct g_part_parms *);
static int g_part_bsd64_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_bsd64_dumpconf(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
static int g_part_bsd64_dumpto(struct g_part_table *, struct g_part_entry *);
static int g_part_bsd64_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_bsd64_name(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_bsd64_probe(struct g_part_table *, struct g_consumer *);
static int g_part_bsd64_read(struct g_part_table *, struct g_consumer *);
static const char *g_part_bsd64_type(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_bsd64_write(struct g_part_table *, struct g_consumer *);
static int g_part_bsd64_resize(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);

static kobj_method_t g_part_bsd64_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_bsd64_add),
	KOBJMETHOD(g_part_bootcode,	g_part_bsd64_bootcode),
	KOBJMETHOD(g_part_create,	g_part_bsd64_create),
	KOBJMETHOD(g_part_destroy,	g_part_bsd64_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_bsd64_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_bsd64_dumpto),
	KOBJMETHOD(g_part_modify,	g_part_bsd64_modify),
	KOBJMETHOD(g_part_resize,	g_part_bsd64_resize),
	KOBJMETHOD(g_part_name,		g_part_bsd64_name),
	KOBJMETHOD(g_part_probe,	g_part_bsd64_probe),
	KOBJMETHOD(g_part_read,		g_part_bsd64_read),
	KOBJMETHOD(g_part_type,		g_part_bsd64_type),
	KOBJMETHOD(g_part_write,	g_part_bsd64_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_bsd64_scheme = {
	"BSD64",
	g_part_bsd64_methods,
	sizeof(struct g_part_bsd64_table),
	.gps_entrysz = sizeof(struct g_part_bsd64_entry),
	.gps_minent = MAXPARTITIONS64,
	.gps_maxent = MAXPARTITIONS64
};
G_PART_SCHEME_DECLARE(g_part_bsd64);
MODULE_VERSION(geom_part_bsd64, 0);

#define	EQUUID(a, b)	(memcmp(a, b, sizeof(struct uuid)) == 0)
static struct uuid bsd64_uuid_unused = GPT_ENT_TYPE_UNUSED;
static struct uuid bsd64_uuid_dfbsd_swap = GPT_ENT_TYPE_DRAGONFLY_SWAP;
static struct uuid bsd64_uuid_dfbsd_ufs1 = GPT_ENT_TYPE_DRAGONFLY_UFS1;
static struct uuid bsd64_uuid_dfbsd_vinum = GPT_ENT_TYPE_DRAGONFLY_VINUM;
static struct uuid bsd64_uuid_dfbsd_ccd = GPT_ENT_TYPE_DRAGONFLY_CCD;
static struct uuid bsd64_uuid_dfbsd_legacy = GPT_ENT_TYPE_DRAGONFLY_LEGACY;
static struct uuid bsd64_uuid_dfbsd_hammer = GPT_ENT_TYPE_DRAGONFLY_HAMMER;
static struct uuid bsd64_uuid_dfbsd_hammer2 = GPT_ENT_TYPE_DRAGONFLY_HAMMER2;
static struct uuid bsd64_uuid_freebsd_boot = GPT_ENT_TYPE_FREEBSD_BOOT;
static struct uuid bsd64_uuid_freebsd_nandfs = GPT_ENT_TYPE_FREEBSD_NANDFS;
static struct uuid bsd64_uuid_freebsd_swap = GPT_ENT_TYPE_FREEBSD_SWAP;
static struct uuid bsd64_uuid_freebsd_ufs = GPT_ENT_TYPE_FREEBSD_UFS;
static struct uuid bsd64_uuid_freebsd_vinum = GPT_ENT_TYPE_FREEBSD_VINUM;
static struct uuid bsd64_uuid_freebsd_zfs = GPT_ENT_TYPE_FREEBSD_ZFS;

struct bsd64_uuid_alias {
	struct uuid *uuid;
	uint8_t fstype;
	int alias;
};
static struct bsd64_uuid_alias dfbsd_alias_match[] = {
	{ &bsd64_uuid_dfbsd_swap, FS_SWAP, G_PART_ALIAS_DFBSD_SWAP },
	{ &bsd64_uuid_dfbsd_ufs1, FS_BSDFFS, G_PART_ALIAS_DFBSD_UFS },
	{ &bsd64_uuid_dfbsd_vinum, FS_VINUM, G_PART_ALIAS_DFBSD_VINUM },
	{ &bsd64_uuid_dfbsd_ccd, FS_CCD, G_PART_ALIAS_DFBSD_CCD },
	{ &bsd64_uuid_dfbsd_legacy, FS_OTHER, G_PART_ALIAS_DFBSD_LEGACY },
	{ &bsd64_uuid_dfbsd_hammer, FS_HAMMER, G_PART_ALIAS_DFBSD_HAMMER },
	{ &bsd64_uuid_dfbsd_hammer2, FS_HAMMER2, G_PART_ALIAS_DFBSD_HAMMER2 },
	{ NULL, 0, 0}
};
static struct bsd64_uuid_alias fbsd_alias_match[] = {
	{ &bsd64_uuid_freebsd_boot, FS_OTHER, G_PART_ALIAS_FREEBSD_BOOT },
	{ &bsd64_uuid_freebsd_swap, FS_OTHER, G_PART_ALIAS_FREEBSD_SWAP },
	{ &bsd64_uuid_freebsd_ufs, FS_OTHER, G_PART_ALIAS_FREEBSD_UFS },
	{ &bsd64_uuid_freebsd_zfs, FS_OTHER, G_PART_ALIAS_FREEBSD_ZFS },
	{ &bsd64_uuid_freebsd_vinum, FS_OTHER, G_PART_ALIAS_FREEBSD_VINUM },
	{ &bsd64_uuid_freebsd_nandfs, FS_OTHER, G_PART_ALIAS_FREEBSD_NANDFS },
	{ NULL, 0, 0}
};

static int
bsd64_parse_type(const char *type, struct g_part_bsd64_entry *entry)
{
	struct uuid tmp;
	const struct bsd64_uuid_alias *uap;
	const char *alias;
	char *p;
	long lt;
	int error;

	if (type[0] == '!') {
		if (type[1] == '\0')
			return (EINVAL);
		lt = strtol(type + 1, &p, 0);
		/* The type specified as number */
		if (*p == '\0') {
			if (lt <= 0 || lt > 255)
				return (EINVAL);
			entry->fstype = lt;
			entry->type_uuid = bsd64_uuid_unused;
			return (0);
		}
		/* The type specified as uuid */
		error = parse_uuid(type + 1, &tmp);
		if (error != 0)
			return (error);
		if (EQUUID(&tmp, &bsd64_uuid_unused))
			return (EINVAL);
		for (uap = &dfbsd_alias_match[0]; uap->uuid != NULL; uap++) {
			if (EQUUID(&tmp, uap->uuid)) {
				/* Prefer fstype for known uuids */
				entry->type_uuid = bsd64_uuid_unused;
				entry->fstype = uap->fstype;
				return (0);
			}
		}
		entry->type_uuid = tmp;
		entry->fstype = FS_OTHER;
		return (0);
	}
	/* The type specified as symbolic alias name */
	for (uap = &fbsd_alias_match[0]; uap->uuid != NULL; uap++) {
		alias = g_part_alias_name(uap->alias);
		if (!strcasecmp(type, alias)) {
			entry->type_uuid = *uap->uuid;
			entry->fstype = uap->fstype;
			return (0);
		}
	}
	for (uap = &dfbsd_alias_match[0]; uap->uuid != NULL; uap++) {
		alias = g_part_alias_name(uap->alias);
		if (!strcasecmp(type, alias)) {
			entry->type_uuid = bsd64_uuid_unused;
			entry->fstype = uap->fstype;
			return (0);
		}
	}
	return (EINVAL);
}

static int
g_part_bsd64_add(struct g_part_table *basetable, struct g_part_entry *baseentry,
    struct g_part_parms *gpp)
{
	struct g_part_bsd64_entry *entry;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	entry = (struct g_part_bsd64_entry *)baseentry;
	if (bsd64_parse_type(gpp->gpp_type, entry) != 0)
		return (EINVAL);
	kern_uuidgen(&entry->stor_uuid, 1);
	return (0);
}

static int
g_part_bsd64_bootcode(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	return (EOPNOTSUPP);
}

#define	PALIGN_SIZE	(1024 * 1024)
#define	PALIGN_MASK	(PALIGN_SIZE - 1)
#define	BLKSIZE		(4 * 1024)
#define	BOOTSIZE	(32 * 1024)
#define	DALIGN_SIZE	(32 * 1024)
static int
g_part_bsd64_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_part_bsd64_table *table;
	struct g_part_entry *baseentry;
	struct g_provider *pp;
	uint64_t blkmask, pbase;
	uint32_t blksize, ressize;

	pp = gpp->gpp_provider;
	if (pp->mediasize < 2* PALIGN_SIZE)
		return (ENOSPC);

	/*
	 * Use at least 4KB block size. Blksize is stored in the d_align.
	 * XXX: Actually it is used just for calculate d_bbase and used
	 * for better alignment in bsdlabel64(8).
	 */
	blksize = pp->sectorsize < BLKSIZE ? BLKSIZE: pp->sectorsize;
	blkmask = blksize - 1;
	/* Reserve enough space for RESPARTITIONS64 partitions. */
	ressize = offsetof(struct disklabel64, d_partitions[RESPARTITIONS64]);
	ressize = (ressize + blkmask) & ~blkmask;
	/*
	 * Reserve enough space for bootcode and align first allocatable
	 * offset to PALIGN_SIZE.
	 * XXX: Currently DragonFlyBSD has 32KB bootcode, but the size could
	 * be bigger, because it is possible change it (it is equal pbase-bbase)
	 * in the bsdlabel64(8).
	 */
	pbase = ressize + ((BOOTSIZE + blkmask) & ~blkmask);
	pbase = (pbase + PALIGN_MASK) & ~PALIGN_MASK;
	/*
	 * Take physical offset into account and make first allocatable
	 * offset 32KB aligned to the start of the physical disk.
	 * XXX: Actually there are no such restrictions, this is how
	 * DragonFlyBSD behaves.
	 */
	pbase += DALIGN_SIZE - pp->stripeoffset % DALIGN_SIZE;

	table = (struct g_part_bsd64_table *)basetable;
	table->d_align = blksize;
	table->d_bbase = ressize / pp->sectorsize;
	table->d_abase = ((pp->mediasize - ressize) &
	    ~blkmask) / pp->sectorsize;
	kern_uuidgen(&table->d_stor_uuid, 1);
	basetable->gpt_first = pbase / pp->sectorsize;
	basetable->gpt_last = table->d_abase - 1; /* XXX */
	/*
	 * Create 'c' partition and make it internal, so user will not be
	 * able use it.
	 */
	baseentry = g_part_new_entry(basetable, RAW_PART + 1, 0, 0);
	baseentry->gpe_internal = 1;
	return (0);
}

static int
g_part_bsd64_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_provider *pp;

	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	if (pp->sectorsize > offsetof(struct disklabel64, d_magic))
		basetable->gpt_smhead |= 1;
	else
		basetable->gpt_smhead |= 3;
	return (0);
}

static void
g_part_bsd64_dumpconf(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct sbuf *sb, const char *indent)
{
	struct g_part_bsd64_table *table;
	struct g_part_bsd64_entry *entry;
	char buf[sizeof(table->d_packname)];

	entry = (struct g_part_bsd64_entry *)baseentry;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs BSD64 xt %u", entry->fstype);
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		sbuf_printf(sb, "%s<rawtype>%u</rawtype>\n", indent,
		    entry->fstype);
		if (!EQUUID(&bsd64_uuid_unused, &entry->type_uuid)) {
			sbuf_printf(sb, "%s<type_uuid>", indent);
			sbuf_printf_uuid(sb, &entry->type_uuid);
			sbuf_printf(sb, "</type_uuid>\n");
		}
		sbuf_printf(sb, "%s<stor_uuid>", indent);
		sbuf_printf_uuid(sb, &entry->stor_uuid);
		sbuf_printf(sb, "</stor_uuid>\n");
	} else {
		/* confxml: scheme information */
		table = (struct g_part_bsd64_table *)basetable;
		sbuf_printf(sb, "%s<bootbase>%ju</bootbase>\n", indent,
		    (uintmax_t)table->d_bbase);
		if (table->d_abase)
			sbuf_printf(sb, "%s<backupbase>%ju</backupbase>\n",
			    indent, (uintmax_t)table->d_abase);
		sbuf_printf(sb, "%s<stor_uuid>", indent);
		sbuf_printf_uuid(sb, &table->d_stor_uuid);
		sbuf_printf(sb, "</stor_uuid>\n");
		sbuf_printf(sb, "%s<label>", indent);
		strncpy(buf, table->d_packname, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		g_conf_printf_escaped(sb, "%s", buf);
		sbuf_printf(sb, "</label>\n");
	}
}

static int
g_part_bsd64_dumpto(struct g_part_table *table, struct g_part_entry *baseentry)
{
	struct g_part_bsd64_entry *entry;

	/* Allow dumping to a swap partition. */
	entry = (struct g_part_bsd64_entry *)baseentry;
	if (entry->fstype == FS_SWAP ||
	    EQUUID(&entry->type_uuid, &bsd64_uuid_dfbsd_swap) ||
	    EQUUID(&entry->type_uuid, &bsd64_uuid_freebsd_swap))
		return (1);
	return (0);
}

static int
g_part_bsd64_modify(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_bsd64_entry *entry;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	entry = (struct g_part_bsd64_entry *)baseentry;
	if (gpp->gpp_parms & G_PART_PARM_TYPE)
		return (bsd64_parse_type(gpp->gpp_type, entry));
	return (0);
}

static int
g_part_bsd64_resize(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_bsd64_table *table;
	struct g_provider *pp;

	if (baseentry == NULL) {
		pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
		table = (struct g_part_bsd64_table *)basetable;
		table->d_abase =
		    rounddown2(pp->mediasize - table->d_bbase * pp->sectorsize,
		        table->d_align) / pp->sectorsize;
		basetable->gpt_last = table->d_abase - 1;
		return (0);
	}
	baseentry->gpe_end = baseentry->gpe_start + gpp->gpp_size - 1;
	return (0);
}

static const char *
g_part_bsd64_name(struct g_part_table *table, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{

	snprintf(buf, bufsz, "%c", 'a' + baseentry->gpe_index - 1);
	return (buf);
}

static int
g_part_bsd64_probe(struct g_part_table *table, struct g_consumer *cp)
{
	struct g_provider *pp;
	uint32_t v;
	int error;
	u_char *buf;

	pp = cp->provider;
	if (pp->mediasize < 2 * PALIGN_SIZE)
		return (ENOSPC);
	v = rounddown2(pp->sectorsize + offsetof(struct disklabel64, d_magic),
		       pp->sectorsize);
	buf = g_read_data(cp, 0, v, &error);
	if (buf == NULL)
		return (error);
	v = le32dec(buf + offsetof(struct disklabel64, d_magic));
	g_free(buf);
	return (v == DISKMAGIC64 ? G_PART_PROBE_PRI_HIGH: ENXIO);
}

static int
g_part_bsd64_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_part_bsd64_table *table;
	struct g_part_bsd64_entry *entry;
	struct g_part_entry *baseentry;
	struct g_provider *pp;
	struct disklabel64 *dlp;
	uint64_t v64, sz;
	uint32_t v32;
	int error, index;
	u_char *buf;

	pp = cp->provider;
	table = (struct g_part_bsd64_table *)basetable;
	v32 = roundup2(sizeof(struct disklabel64), pp->sectorsize);
	buf = g_read_data(cp, 0, v32, &error);
	if (buf == NULL)
		return (error);

	dlp = (struct disklabel64 *)buf;
	basetable->gpt_entries = le32toh(dlp->d_npartitions);
	if (basetable->gpt_entries > MAXPARTITIONS64 ||
	    basetable->gpt_entries < 1)
		goto invalid_label;
	v32 = le32toh(dlp->d_crc);
	dlp->d_crc = 0;
	if (crc32(&dlp->d_magic, offsetof(struct disklabel64,
	    d_partitions[basetable->gpt_entries]) -
	    offsetof(struct disklabel64, d_magic)) != v32)
		goto invalid_label;
	table->d_align = le32toh(dlp->d_align);
	if (table->d_align == 0 || (table->d_align & (pp->sectorsize - 1)))
		goto invalid_label;
	if (le64toh(dlp->d_total_size) > pp->mediasize)
		goto invalid_label;
	v64 = le64toh(dlp->d_pbase);
	if (v64 % pp->sectorsize)
		goto invalid_label;
	basetable->gpt_first = v64 / pp->sectorsize;
	v64 = le64toh(dlp->d_pstop);
	if (v64 % pp->sectorsize)
		goto invalid_label;
	basetable->gpt_last = v64 / pp->sectorsize;
	basetable->gpt_isleaf = 1;
	v64 = le64toh(dlp->d_bbase);
	if (v64 % pp->sectorsize)
		goto invalid_label;
	table->d_bbase = v64 / pp->sectorsize;
	v64 = le64toh(dlp->d_abase);
	if (v64 % pp->sectorsize)
		goto invalid_label;
	table->d_abase = v64 / pp->sectorsize;
	le_uuid_dec(&dlp->d_stor_uuid, &table->d_stor_uuid);
	for (index = basetable->gpt_entries - 1; index >= 0; index--) {
		if (index == RAW_PART) {
			/* Skip 'c' partition. */
			baseentry = g_part_new_entry(basetable,
			    index + 1, 0, 0);
			baseentry->gpe_internal = 1;
			continue;
		}
		v64 = le64toh(dlp->d_partitions[index].p_boffset);
		sz = le64toh(dlp->d_partitions[index].p_bsize);
		if (sz == 0 && v64 == 0)
			continue;
		if (sz == 0 || (v64 % pp->sectorsize) || (sz % pp->sectorsize))
			goto invalid_label;
		baseentry = g_part_new_entry(basetable, index + 1,
		    v64 / pp->sectorsize, (v64 + sz) / pp->sectorsize - 1);
		entry = (struct g_part_bsd64_entry *)baseentry;
		le_uuid_dec(&dlp->d_partitions[index].p_type_uuid,
		    &entry->type_uuid);
		le_uuid_dec(&dlp->d_partitions[index].p_stor_uuid,
		    &entry->stor_uuid);
		entry->fstype = dlp->d_partitions[index].p_fstype;
	}
	bcopy(dlp->d_reserved0, table->d_reserved0,
	    sizeof(table->d_reserved0));
	bcopy(dlp->d_packname, table->d_packname, sizeof(table->d_packname));
	bcopy(dlp->d_reserved, table->d_reserved, sizeof(table->d_reserved));
	g_free(buf);
	return (0);

invalid_label:
	g_free(buf);
	return (EINVAL);
}

static const char *
g_part_bsd64_type(struct g_part_table *basetable, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_bsd64_entry *entry;
	struct bsd64_uuid_alias *uap;

	entry = (struct g_part_bsd64_entry *)baseentry;
	if (entry->fstype != FS_OTHER) {
		for (uap = &dfbsd_alias_match[0]; uap->uuid != NULL; uap++)
			if (uap->fstype == entry->fstype)
				return (g_part_alias_name(uap->alias));
	} else {
		for (uap = &fbsd_alias_match[0]; uap->uuid != NULL; uap++)
			if (EQUUID(uap->uuid, &entry->type_uuid))
				return (g_part_alias_name(uap->alias));
		for (uap = &dfbsd_alias_match[0]; uap->uuid != NULL; uap++)
			if (EQUUID(uap->uuid, &entry->type_uuid))
				return (g_part_alias_name(uap->alias));
	}
	if (EQUUID(&bsd64_uuid_unused, &entry->type_uuid))
		snprintf(buf, bufsz, "!%d", entry->fstype);
	else {
		buf[0] = '!';
		snprintf_uuid(buf + 1, bufsz - 1, &entry->type_uuid);
	}
	return (buf);
}

static int
g_part_bsd64_write(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_entry *baseentry;
	struct g_part_bsd64_entry *entry;
	struct g_part_bsd64_table *table;
	struct disklabel64 *dlp;
	uint32_t v, sz;
	int error, index;

	pp = cp->provider;
	table = (struct g_part_bsd64_table *)basetable;
	sz = roundup2(sizeof(struct disklabel64), pp->sectorsize);
	dlp = g_malloc(sz, M_WAITOK | M_ZERO);

	memcpy(dlp->d_reserved0, table->d_reserved0,
	    sizeof(table->d_reserved0));
	memcpy(dlp->d_packname, table->d_packname, sizeof(table->d_packname));
	memcpy(dlp->d_reserved, table->d_reserved, sizeof(table->d_reserved));
	le32enc(&dlp->d_magic, DISKMAGIC64);
	le32enc(&dlp->d_align, table->d_align);
	le32enc(&dlp->d_npartitions, basetable->gpt_entries);
	le_uuid_enc(&dlp->d_stor_uuid, &table->d_stor_uuid);
	le64enc(&dlp->d_total_size, pp->mediasize);
	le64enc(&dlp->d_bbase, table->d_bbase * pp->sectorsize);
	le64enc(&dlp->d_pbase, basetable->gpt_first * pp->sectorsize);
	le64enc(&dlp->d_pstop, basetable->gpt_last * pp->sectorsize);
	le64enc(&dlp->d_abase, table->d_abase * pp->sectorsize);

	LIST_FOREACH(baseentry, &basetable->gpt_entry, gpe_entry) {
		if (baseentry->gpe_deleted)
			continue;
		index = baseentry->gpe_index - 1;
		entry = (struct g_part_bsd64_entry *)baseentry;
		if (index == RAW_PART)
			continue;
		le64enc(&dlp->d_partitions[index].p_boffset,
		    baseentry->gpe_start * pp->sectorsize);
		le64enc(&dlp->d_partitions[index].p_bsize, pp->sectorsize *
		    (baseentry->gpe_end - baseentry->gpe_start + 1));
		dlp->d_partitions[index].p_fstype = entry->fstype;
		le_uuid_enc(&dlp->d_partitions[index].p_type_uuid,
		    &entry->type_uuid);
		le_uuid_enc(&dlp->d_partitions[index].p_stor_uuid,
		    &entry->stor_uuid);
	}
	/* Calculate checksum. */
	v = offsetof(struct disklabel64,
	    d_partitions[basetable->gpt_entries]) -
	    offsetof(struct disklabel64, d_magic);
	le32enc(&dlp->d_crc, crc32(&dlp->d_magic, v));
	error = g_write_data(cp, 0, dlp, sz);
	g_free(dlp);
	return (error);
}

