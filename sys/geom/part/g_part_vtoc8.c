/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Marcel Moolenaar
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
#include <sys/endian.h>
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
#include <sys/vtoc.h>
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_vtoc8, "GEOM partitioning class for SMI VTOC8 disk labels");

struct g_part_vtoc8_table {
	struct g_part_table	base;
	struct vtoc8		vtoc;
	uint32_t		secpercyl;
};

static int g_part_vtoc8_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_vtoc8_create(struct g_part_table *, struct g_part_parms *);
static int g_part_vtoc8_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_vtoc8_dumpconf(struct g_part_table *,
    struct g_part_entry *, struct sbuf *, const char *);
static int g_part_vtoc8_dumpto(struct g_part_table *, struct g_part_entry *);
static int g_part_vtoc8_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_vtoc8_name(struct g_part_table *,
    struct g_part_entry *, char *, size_t);
static int g_part_vtoc8_probe(struct g_part_table *, struct g_consumer *);
static int g_part_vtoc8_read(struct g_part_table *, struct g_consumer *);
static const char *g_part_vtoc8_type(struct g_part_table *,
    struct g_part_entry *, char *, size_t);
static int g_part_vtoc8_write(struct g_part_table *, struct g_consumer *);
static int g_part_vtoc8_resize(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);

static kobj_method_t g_part_vtoc8_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_vtoc8_add),
	KOBJMETHOD(g_part_create,	g_part_vtoc8_create),
	KOBJMETHOD(g_part_destroy,	g_part_vtoc8_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_vtoc8_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_vtoc8_dumpto),
	KOBJMETHOD(g_part_modify,	g_part_vtoc8_modify),
	KOBJMETHOD(g_part_resize,	g_part_vtoc8_resize),
	KOBJMETHOD(g_part_name,		g_part_vtoc8_name),
	KOBJMETHOD(g_part_probe,	g_part_vtoc8_probe),
	KOBJMETHOD(g_part_read,		g_part_vtoc8_read),
	KOBJMETHOD(g_part_type,		g_part_vtoc8_type),
	KOBJMETHOD(g_part_write,	g_part_vtoc8_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_vtoc8_scheme = {
	"VTOC8",
	g_part_vtoc8_methods,
	sizeof(struct g_part_vtoc8_table),
	.gps_entrysz = sizeof(struct g_part_entry),
	.gps_minent = VTOC8_NPARTS,
	.gps_maxent = VTOC8_NPARTS,
};
G_PART_SCHEME_DECLARE(g_part_vtoc8);
MODULE_VERSION(geom_part_vtoc8, 0);

static int
vtoc8_parse_type(const char *type, uint16_t *tag)
{
	const char *alias;
	char *endp;
	long lt;

	if (type[0] == '!') {
		lt = strtol(type + 1, &endp, 0);
		if (type[1] == '\0' || *endp != '\0' || lt <= 0 ||
		    lt >= 65536)
			return (EINVAL);
		*tag = (uint16_t)lt;
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_NANDFS);
	if (!strcasecmp(type, alias)) {
		*tag = VTOC_TAG_FREEBSD_NANDFS;
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_SWAP);
	if (!strcasecmp(type, alias)) {
		*tag = VTOC_TAG_FREEBSD_SWAP;
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_UFS);
	if (!strcasecmp(type, alias)) {
		*tag = VTOC_TAG_FREEBSD_UFS;
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_VINUM);
	if (!strcasecmp(type, alias)) {
		*tag = VTOC_TAG_FREEBSD_VINUM;
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_ZFS);
	if (!strcasecmp(type, alias)) {
		*tag = VTOC_TAG_FREEBSD_ZFS;
		return (0);
	}
	return (EINVAL);
}

static int
vtoc8_align(struct g_part_vtoc8_table *table, uint64_t *start, uint64_t *size)
{

	if (*size < table->secpercyl)
		return (EINVAL);
	if (start != NULL && (*start % table->secpercyl)) {
		*size += (*start % table->secpercyl) - table->secpercyl;
		*start -= (*start % table->secpercyl) - table->secpercyl;
	}
	if (*size % table->secpercyl)
		*size -= (*size % table->secpercyl);
	if (*size < table->secpercyl)
		return (EINVAL);
	return (0);
}

static int
g_part_vtoc8_add(struct g_part_table *basetable, struct g_part_entry *entry,
    struct g_part_parms *gpp)
{
	struct g_part_vtoc8_table *table;
	int error, index;
	uint64_t start, size;
	uint16_t tag;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	error = vtoc8_parse_type(gpp->gpp_type, &tag);
	if (error)
		return (error);

	table = (struct g_part_vtoc8_table *)basetable;
	index = entry->gpe_index - 1;
	start = gpp->gpp_start;
	size = gpp->gpp_size;
	if (vtoc8_align(table, &start, &size) != 0)
		return (EINVAL);

	KASSERT(entry->gpe_start <= start, (__func__));
	KASSERT(entry->gpe_end >= start + size - 1, (__func__));
	entry->gpe_start = start;
	entry->gpe_end = start + size - 1;

	be16enc(&table->vtoc.part[index].tag, tag);
	be16enc(&table->vtoc.part[index].flag, 0);
	be32enc(&table->vtoc.timestamp[index], 0);
	be32enc(&table->vtoc.map[index].cyl, start / table->secpercyl);
	be32enc(&table->vtoc.map[index].nblks, size);
	return (0);
}

static int
g_part_vtoc8_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_provider *pp;
	struct g_part_entry *entry;
	struct g_part_vtoc8_table *table;
	uint64_t msize;
	uint32_t acyls, ncyls, pcyls;

	pp = gpp->gpp_provider;

	if (pp->sectorsize < sizeof(struct vtoc8))
		return (ENOSPC);
	if (pp->sectorsize > sizeof(struct vtoc8))
		return (ENXIO);

	table = (struct g_part_vtoc8_table *)basetable;

	msize = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);
	table->secpercyl = basetable->gpt_sectors * basetable->gpt_heads;
	pcyls = msize / table->secpercyl;
	acyls = 2;
	ncyls = pcyls - acyls;
	msize = ncyls * table->secpercyl;

	sprintf(table->vtoc.ascii, "FreeBSD%lldM cyl %u alt %u hd %u sec %u",
	    (long long)(msize / 2048), ncyls, acyls, basetable->gpt_heads,
	    basetable->gpt_sectors);
	be32enc(&table->vtoc.version, VTOC_VERSION);
	be16enc(&table->vtoc.nparts, VTOC8_NPARTS);
	be32enc(&table->vtoc.sanity, VTOC_SANITY);
	be16enc(&table->vtoc.rpm, 3600);
	be16enc(&table->vtoc.physcyls, pcyls);
	be16enc(&table->vtoc.ncyls, ncyls);
	be16enc(&table->vtoc.altcyls, acyls);
	be16enc(&table->vtoc.nheads, basetable->gpt_heads);
	be16enc(&table->vtoc.nsecs, basetable->gpt_sectors);
	be16enc(&table->vtoc.magic, VTOC_MAGIC);

	basetable->gpt_first = 0;
	basetable->gpt_last = msize - 1;
	basetable->gpt_isleaf = 1;

	entry = g_part_new_entry(basetable, VTOC_RAW_PART + 1,
	    basetable->gpt_first, basetable->gpt_last);
	entry->gpe_internal = 1;
	be16enc(&table->vtoc.part[VTOC_RAW_PART].tag, VTOC_TAG_BACKUP);
	be32enc(&table->vtoc.map[VTOC_RAW_PART].nblks, msize);
	return (0);
}

static int
g_part_vtoc8_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	/* Wipe the first sector to clear the partitioning. */
	basetable->gpt_smhead |= 1;
	return (0);
}

static void
g_part_vtoc8_dumpconf(struct g_part_table *basetable,
    struct g_part_entry *entry, struct sbuf *sb, const char *indent)
{
	struct g_part_vtoc8_table *table;

	table = (struct g_part_vtoc8_table *)basetable;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs SUN sc %u hd %u alt %u",
		    be16dec(&table->vtoc.nsecs), be16dec(&table->vtoc.nheads),
		    be16dec(&table->vtoc.altcyls));
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		sbuf_printf(sb, "%s<rawtype>%u</rawtype>\n", indent,
		    be16dec(&table->vtoc.part[entry->gpe_index - 1].tag));
	} else {
		/* confxml: scheme information */
	}
}

static int
g_part_vtoc8_dumpto(struct g_part_table *basetable,
    struct g_part_entry *entry)
{
	struct g_part_vtoc8_table *table;
	uint16_t tag;

	/*
	 * Allow dumping to a swap partition or a partition that
	 * has no type.
	 */
	table = (struct g_part_vtoc8_table *)basetable;
	tag = be16dec(&table->vtoc.part[entry->gpe_index - 1].tag);
	return ((tag == 0 || tag == VTOC_TAG_FREEBSD_SWAP ||
	    tag == VTOC_TAG_SWAP) ? 1 : 0);
}

static int
g_part_vtoc8_modify(struct g_part_table *basetable,
    struct g_part_entry *entry, struct g_part_parms *gpp)
{
	struct g_part_vtoc8_table *table;
	int error;
	uint16_t tag;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	table = (struct g_part_vtoc8_table *)basetable;
	if (gpp->gpp_parms & G_PART_PARM_TYPE) {
		error = vtoc8_parse_type(gpp->gpp_type, &tag);
		if (error)
			return(error);

		be16enc(&table->vtoc.part[entry->gpe_index - 1].tag, tag);
	}
	return (0);
}

static int
vtoc8_set_rawsize(struct g_part_table *basetable, struct g_provider *pp)
{
	struct g_part_vtoc8_table *table;
	struct g_part_entry *baseentry;
	off_t msize;
	uint32_t acyls, ncyls, pcyls;

	table = (struct g_part_vtoc8_table *)basetable;
	msize = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);
	pcyls = msize / table->secpercyl;
	if (pcyls > UINT16_MAX)
		return (ERANGE);
	acyls = be16dec(&table->vtoc.altcyls);
	ncyls = pcyls - acyls;
	msize = ncyls * table->secpercyl;
	basetable->gpt_last = msize - 1;

	bzero(table->vtoc.ascii, sizeof(table->vtoc.ascii));
	sprintf(table->vtoc.ascii, "FreeBSD%lldM cyl %u alt %u hd %u sec %u",
	    (long long)(msize / 2048), ncyls, acyls, basetable->gpt_heads,
	    basetable->gpt_sectors);
	be16enc(&table->vtoc.physcyls, pcyls);
	be16enc(&table->vtoc.ncyls, ncyls);
	be32enc(&table->vtoc.map[VTOC_RAW_PART].nblks, msize);
	if (be32dec(&table->vtoc.sanity) == VTOC_SANITY)
		be16enc(&table->vtoc.part[VTOC_RAW_PART].tag, VTOC_TAG_BACKUP);
	LIST_FOREACH(baseentry, &basetable->gpt_entry, gpe_entry) {
		if (baseentry->gpe_index == VTOC_RAW_PART + 1) {
			baseentry->gpe_end = basetable->gpt_last;
			return (0);
		}
	}
	return (ENXIO);
}

static int
g_part_vtoc8_resize(struct g_part_table *basetable,
    struct g_part_entry *entry, struct g_part_parms *gpp)
{
	struct g_part_vtoc8_table *table;
	struct g_provider *pp;
	uint64_t size;

	if (entry == NULL) {
		pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
		return (vtoc8_set_rawsize(basetable, pp));
	}
	table = (struct g_part_vtoc8_table *)basetable;
	size = gpp->gpp_size;
	if (vtoc8_align(table, NULL, &size) != 0)
		return (EINVAL);
	/* XXX: prevent unexpected shrinking. */
	pp = entry->gpe_pp;
	if ((g_debugflags & 0x10) == 0 && size < gpp->gpp_size &&
	    pp->mediasize / pp->sectorsize > size)
		return (EBUSY);
	entry->gpe_end = entry->gpe_start + size - 1;
	be32enc(&table->vtoc.map[entry->gpe_index - 1].nblks, size);

	return (0);
}

static const char *
g_part_vtoc8_name(struct g_part_table *table, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{

	snprintf(buf, bufsz, "%c", 'a' + baseentry->gpe_index - 1);
	return (buf);
}

static int
g_part_vtoc8_probe(struct g_part_table *table, struct g_consumer *cp)
{
	struct g_provider *pp;
	u_char *buf;
	int error, ofs, res;
	uint16_t cksum, magic;

	pp = cp->provider;

	/* Sanity-check the provider. */
	if (pp->sectorsize != sizeof(struct vtoc8))
		return (ENOSPC);

	/* Check that there's a disklabel. */
	buf = g_read_data(cp, 0, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);

	res = ENXIO;	/* Assume mismatch */

	/* Check the magic */
	magic = be16dec(buf + offsetof(struct vtoc8, magic));
	if (magic != VTOC_MAGIC)
		goto out;

	/* Check the sum */
	cksum = 0;
	for (ofs = 0; ofs < sizeof(struct vtoc8); ofs += 2)
		cksum ^= be16dec(buf + ofs);
	if (cksum != 0)
		goto out;

	res = G_PART_PROBE_PRI_NORM;

 out:
	g_free(buf);
	return (res);
}

static int
g_part_vtoc8_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_vtoc8_table *table;
	struct g_part_entry *entry;
	u_char *buf;
	off_t chs, msize;
	uint64_t offset, size;
	u_int cyls, heads, sectors;
	int error, index, withtags;
	uint16_t tag;

	pp = cp->provider;
	buf = g_read_data(cp, 0, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);

	table = (struct g_part_vtoc8_table *)basetable;
	bcopy(buf, &table->vtoc, sizeof(table->vtoc));
	g_free(buf);

	msize = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);
	sectors = be16dec(&table->vtoc.nsecs);
	if (sectors < 1)
		goto invalid_label;
	if (sectors != basetable->gpt_sectors && !basetable->gpt_fixgeom) {
		g_part_geometry_heads(msize, sectors, &chs, &heads);
		if (chs != 0) {
			basetable->gpt_sectors = sectors;
			basetable->gpt_heads = heads;
		}
	}

	heads = be16dec(&table->vtoc.nheads);
	if (heads < 1)
		goto invalid_label;
	if (heads != basetable->gpt_heads && !basetable->gpt_fixgeom)
		basetable->gpt_heads = heads;
	/*
	 * Except for ATA disks > 32GB, Solaris uses the native geometry
	 * as reported by the target for the labels while da(4) typically
	 * uses a synthetic one so we don't complain too loudly if these
	 * geometries don't match.
	 */
	if (bootverbose && (sectors != basetable->gpt_sectors ||
	    heads != basetable->gpt_heads))
		printf("GEOM: %s: geometry does not match VTOC8 label "
		    "(label: %uh,%us GEOM: %uh,%us).\n", pp->name, heads,
		    sectors, basetable->gpt_heads, basetable->gpt_sectors);

	table->secpercyl = heads * sectors;
	cyls = be16dec(&table->vtoc.ncyls);
	chs = cyls * table->secpercyl;
	if (chs < 1 || chs > msize)
		goto invalid_label;

	basetable->gpt_first = 0;
	basetable->gpt_last = chs - 1;
	basetable->gpt_isleaf = 1;

	withtags = (be32dec(&table->vtoc.sanity) == VTOC_SANITY) ? 1 : 0;
	if (!withtags) {
		printf("GEOM: %s: adding VTOC8 information.\n", pp->name);
		be32enc(&table->vtoc.version, VTOC_VERSION);
		bzero(&table->vtoc.volume, VTOC_VOLUME_LEN);
		be16enc(&table->vtoc.nparts, VTOC8_NPARTS);
		bzero(&table->vtoc.part, sizeof(table->vtoc.part));
		be32enc(&table->vtoc.sanity, VTOC_SANITY);
	}

	basetable->gpt_entries = be16dec(&table->vtoc.nparts);
	if (basetable->gpt_entries < g_part_vtoc8_scheme.gps_minent ||
	    basetable->gpt_entries > g_part_vtoc8_scheme.gps_maxent)
		goto invalid_label;

	for (index = basetable->gpt_entries - 1; index >= 0; index--) {
		offset = be32dec(&table->vtoc.map[index].cyl) *
		    table->secpercyl;
		size = be32dec(&table->vtoc.map[index].nblks);
		if (size == 0)
			continue;
		if (withtags)
			tag = be16dec(&table->vtoc.part[index].tag);
		else
			tag = (index == VTOC_RAW_PART)
			    ? VTOC_TAG_BACKUP
			    : VTOC_TAG_UNASSIGNED;

		if (index == VTOC_RAW_PART && tag != VTOC_TAG_BACKUP)
			continue;
		if (index != VTOC_RAW_PART && tag == VTOC_TAG_BACKUP)
			continue;
		entry = g_part_new_entry(basetable, index + 1, offset,
		    offset + size - 1);
		if (tag == VTOC_TAG_BACKUP)
			entry->gpe_internal = 1;

		if (!withtags)
			be16enc(&table->vtoc.part[index].tag, tag);
	}

	return (0);

 invalid_label:
	printf("GEOM: %s: invalid VTOC8 label.\n", pp->name);
	return (EINVAL);
}

static const char *
g_part_vtoc8_type(struct g_part_table *basetable, struct g_part_entry *entry,
    char *buf, size_t bufsz)
{
	struct g_part_vtoc8_table *table;
	uint16_t tag;

	table = (struct g_part_vtoc8_table *)basetable;
	tag = be16dec(&table->vtoc.part[entry->gpe_index - 1].tag);
	if (tag == VTOC_TAG_FREEBSD_NANDFS)
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_NANDFS));
	if (tag == VTOC_TAG_FREEBSD_SWAP)
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_SWAP));
	if (tag == VTOC_TAG_FREEBSD_UFS)
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_UFS));
	if (tag == VTOC_TAG_FREEBSD_VINUM)
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_VINUM));
	if (tag == VTOC_TAG_FREEBSD_ZFS)
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_ZFS));
	snprintf(buf, bufsz, "!%d", tag);
	return (buf);
}

static int
g_part_vtoc8_write(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_entry *entry;
	struct g_part_vtoc8_table *table;
	uint16_t sum;
	u_char *p;
	int error, index, match, offset;

	pp = cp->provider;
	table = (struct g_part_vtoc8_table *)basetable;
	entry = LIST_FIRST(&basetable->gpt_entry);
	for (index = 0; index < basetable->gpt_entries; index++) {
		match = (entry != NULL && index == entry->gpe_index - 1)
		    ? 1 : 0;
		if (match) {
			if (entry->gpe_deleted) {
				be16enc(&table->vtoc.part[index].tag, 0);
				be16enc(&table->vtoc.part[index].flag, 0);
				be32enc(&table->vtoc.map[index].cyl, 0);
				be32enc(&table->vtoc.map[index].nblks, 0);
			}
			entry = LIST_NEXT(entry, gpe_entry);
		}
	}

	/* Calculate checksum. */
	sum = 0;
	p = (void *)&table->vtoc;
	for (offset = 0; offset < sizeof(table->vtoc) - 2; offset += 2)
		sum ^= be16dec(p + offset);
	be16enc(&table->vtoc.cksum, sum);

	error = g_write_data(cp, 0, p, pp->sectorsize);
	return (error);
}
