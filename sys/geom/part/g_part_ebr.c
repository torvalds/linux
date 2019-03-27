/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Marcel Moolenaar
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

#include "opt_geom.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/diskmbr.h>
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
#include <geom/geom.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_ebr,
    "GEOM partitioning class for extended boot records support");
#if defined(GEOM_PART_EBR_COMPAT)
FEATURE(geom_part_ebr_compat,
    "GEOM EBR partitioning class: backward-compatible partition names");
#endif

#define	EBRSIZE		512

struct g_part_ebr_table {
	struct g_part_table	base;
#ifndef GEOM_PART_EBR_COMPAT
	u_char		ebr[EBRSIZE];
#endif
};

struct g_part_ebr_entry {
	struct g_part_entry	base;
	struct dos_partition	ent;
};

static int g_part_ebr_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_ebr_create(struct g_part_table *, struct g_part_parms *);
static int g_part_ebr_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_ebr_dumpconf(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
static int g_part_ebr_dumpto(struct g_part_table *, struct g_part_entry *);
#if defined(GEOM_PART_EBR_COMPAT)
static void g_part_ebr_fullname(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
#endif
static int g_part_ebr_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_ebr_name(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_ebr_precheck(struct g_part_table *, enum g_part_ctl,
    struct g_part_parms *);
static int g_part_ebr_probe(struct g_part_table *, struct g_consumer *);
static int g_part_ebr_read(struct g_part_table *, struct g_consumer *);
static int g_part_ebr_setunset(struct g_part_table *, struct g_part_entry *,
    const char *, unsigned int);
static const char *g_part_ebr_type(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_ebr_write(struct g_part_table *, struct g_consumer *);
static int g_part_ebr_resize(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);

static kobj_method_t g_part_ebr_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_ebr_add),
	KOBJMETHOD(g_part_create,	g_part_ebr_create),
	KOBJMETHOD(g_part_destroy,	g_part_ebr_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_ebr_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_ebr_dumpto),
#if defined(GEOM_PART_EBR_COMPAT)
	KOBJMETHOD(g_part_fullname,	g_part_ebr_fullname),
#endif
	KOBJMETHOD(g_part_modify,	g_part_ebr_modify),
	KOBJMETHOD(g_part_name,		g_part_ebr_name),
	KOBJMETHOD(g_part_precheck,	g_part_ebr_precheck),
	KOBJMETHOD(g_part_probe,	g_part_ebr_probe),
	KOBJMETHOD(g_part_read,		g_part_ebr_read),
	KOBJMETHOD(g_part_resize,	g_part_ebr_resize),
	KOBJMETHOD(g_part_setunset,	g_part_ebr_setunset),
	KOBJMETHOD(g_part_type,		g_part_ebr_type),
	KOBJMETHOD(g_part_write,	g_part_ebr_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_ebr_scheme = {
	"EBR",
	g_part_ebr_methods,
	sizeof(struct g_part_ebr_table),
	.gps_entrysz = sizeof(struct g_part_ebr_entry),
	.gps_minent = 1,
	.gps_maxent = INT_MAX,
};
G_PART_SCHEME_DECLARE(g_part_ebr);
MODULE_VERSION(geom_part_ebr, 0);

static struct g_part_ebr_alias {
	u_char		typ;
	int		alias;
} ebr_alias_match[] = {
	{ DOSPTYP_386BSD,	G_PART_ALIAS_FREEBSD },
	{ DOSPTYP_EFI,		G_PART_ALIAS_EFI },
	{ DOSPTYP_FAT32,	G_PART_ALIAS_MS_FAT32 },
	{ DOSPTYP_FAT32LBA,	G_PART_ALIAS_MS_FAT32LBA },
	{ DOSPTYP_LINLVM,	G_PART_ALIAS_LINUX_LVM },
	{ DOSPTYP_LINRAID,	G_PART_ALIAS_LINUX_RAID },
	{ DOSPTYP_LINSWP,	G_PART_ALIAS_LINUX_SWAP },
	{ DOSPTYP_LINUX,	G_PART_ALIAS_LINUX_DATA },
	{ DOSPTYP_NTFS,		G_PART_ALIAS_MS_NTFS },
};

static void ebr_set_chs(struct g_part_table *, uint32_t, u_char *, u_char *,
    u_char *);

static void
ebr_entry_decode(const char *p, struct dos_partition *ent)
{
	ent->dp_flag = p[0];
	ent->dp_shd = p[1];
	ent->dp_ssect = p[2];
	ent->dp_scyl = p[3];
	ent->dp_typ = p[4];
	ent->dp_ehd = p[5];
	ent->dp_esect = p[6];
	ent->dp_ecyl = p[7];
	ent->dp_start = le32dec(p + 8);
	ent->dp_size = le32dec(p + 12);
}

static void
ebr_entry_link(struct g_part_table *table, uint32_t start, uint32_t end,
   u_char *buf)
{

	buf[0] = 0 /* dp_flag */;
	ebr_set_chs(table, start, &buf[3] /* dp_scyl */, &buf[1] /* dp_shd */,
	    &buf[2] /* dp_ssect */);
	buf[4] = 5 /* dp_typ */;
	ebr_set_chs(table, end, &buf[7] /* dp_ecyl */, &buf[5] /* dp_ehd */,
	    &buf[6] /* dp_esect */);
	le32enc(buf + 8, start);
	le32enc(buf + 12, end - start + 1);
}

static int
ebr_parse_type(const char *type, u_char *dp_typ)
{
	const char *alias;
	char *endp;
	long lt;
	int i;

	if (type[0] == '!') {
		lt = strtol(type + 1, &endp, 0);
		if (type[1] == '\0' || *endp != '\0' || lt <= 0 || lt >= 256)
			return (EINVAL);
		*dp_typ = (u_char)lt;
		return (0);
	}
	for (i = 0; i < nitems(ebr_alias_match); i++) {
		alias = g_part_alias_name(ebr_alias_match[i].alias);
		if (strcasecmp(type, alias) == 0) {
			*dp_typ = ebr_alias_match[i].typ;
			return (0);
		}
	}
	return (EINVAL);
}


static void
ebr_set_chs(struct g_part_table *table, uint32_t lba, u_char *cylp, u_char *hdp,
    u_char *secp)
{
	uint32_t cyl, hd, sec;

	sec = lba % table->gpt_sectors + 1;
	lba /= table->gpt_sectors;
	hd = lba % table->gpt_heads;
	lba /= table->gpt_heads;
	cyl = lba;
	if (cyl > 1023)
		sec = hd = cyl = ~0;

	*cylp = cyl & 0xff;
	*hdp = hd & 0xff;
	*secp = (sec & 0x3f) | ((cyl >> 2) & 0xc0);
}

static int
ebr_align(struct g_part_table *basetable, uint32_t *start, uint32_t *size)
{
	uint32_t sectors;

	sectors = basetable->gpt_sectors;
	if (*size < 2 * sectors)
		return (EINVAL);
	if (*start % sectors) {
		*size += (*start % sectors) - sectors;
		*start -= (*start % sectors) - sectors;
	}
	if (*size % sectors)
		*size -= (*size % sectors);
	if (*size < 2 * sectors)
		return (EINVAL);
	return (0);
}


static int
g_part_ebr_add(struct g_part_table *basetable, struct g_part_entry *baseentry,
    struct g_part_parms *gpp)
{
	struct g_provider *pp;
	struct g_part_ebr_entry *entry;
	uint32_t start, size;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	entry = (struct g_part_ebr_entry *)baseentry;
	start = gpp->gpp_start;
	size = gpp->gpp_size;
	if (ebr_align(basetable, &start, &size) != 0)
		return (EINVAL);
	if (baseentry->gpe_deleted)
		bzero(&entry->ent, sizeof(entry->ent));

	KASSERT(baseentry->gpe_start <= start, ("%s", __func__));
	KASSERT(baseentry->gpe_end >= start + size - 1, ("%s", __func__));
	baseentry->gpe_index = (start / basetable->gpt_sectors) + 1;
	baseentry->gpe_offset =
	    (off_t)(start + basetable->gpt_sectors) * pp->sectorsize;
	baseentry->gpe_start = start;
	baseentry->gpe_end = start + size - 1;
	entry->ent.dp_start = basetable->gpt_sectors;
	entry->ent.dp_size = size - basetable->gpt_sectors;
	ebr_set_chs(basetable, entry->ent.dp_start, &entry->ent.dp_scyl,
	    &entry->ent.dp_shd, &entry->ent.dp_ssect);
	ebr_set_chs(basetable, baseentry->gpe_end, &entry->ent.dp_ecyl,
	    &entry->ent.dp_ehd, &entry->ent.dp_esect);
	return (ebr_parse_type(gpp->gpp_type, &entry->ent.dp_typ));
}

static int
g_part_ebr_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	char type[64];
	struct g_consumer *cp;
	struct g_provider *pp;
	uint32_t msize;
	int error;

	pp = gpp->gpp_provider;

	if (pp->sectorsize < EBRSIZE)
		return (ENOSPC);
	if (pp->sectorsize > 4096)
		return (ENXIO);

	/* Check that we have a parent and that it's a MBR. */
	if (basetable->gpt_depth == 0)
		return (ENXIO);
	cp = LIST_FIRST(&pp->consumers);
	error = g_getattr("PART::scheme", cp, &type);
	if (error != 0)
		return (error);
	if (strcmp(type, "MBR") != 0)
		return (ENXIO);
	error = g_getattr("PART::type", cp, &type);
	if (error != 0)
		return (error);
	if (strcmp(type, "ebr") != 0)
		return (ENXIO);

	msize = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);
	basetable->gpt_first = 0;
	basetable->gpt_last = msize - 1;
	basetable->gpt_entries = msize / basetable->gpt_sectors;
	return (0);
}

static int
g_part_ebr_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	/* Wipe the first sector to clear the partitioning. */
	basetable->gpt_smhead |= 1;
	return (0);
}

static void
g_part_ebr_dumpconf(struct g_part_table *table, struct g_part_entry *baseentry,
    struct sbuf *sb, const char *indent)
{
	struct g_part_ebr_entry *entry;

	entry = (struct g_part_ebr_entry *)baseentry;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs MBREXT xt %u", entry->ent.dp_typ);
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		sbuf_printf(sb, "%s<rawtype>%u</rawtype>\n", indent,
		    entry->ent.dp_typ);
		if (entry->ent.dp_flag & 0x80)
			sbuf_printf(sb, "%s<attrib>active</attrib>\n", indent);
	} else {
		/* confxml: scheme information */
	}
}

static int
g_part_ebr_dumpto(struct g_part_table *table, struct g_part_entry *baseentry)
{
	struct g_part_ebr_entry *entry;

	/* Allow dumping to a FreeBSD partition or Linux swap partition only. */
	entry = (struct g_part_ebr_entry *)baseentry;
	return ((entry->ent.dp_typ == DOSPTYP_386BSD ||
	    entry->ent.dp_typ == DOSPTYP_LINSWP) ? 1 : 0);
}

#if defined(GEOM_PART_EBR_COMPAT)
static void
g_part_ebr_fullname(struct g_part_table *table, struct g_part_entry *entry,
    struct sbuf *sb, const char *pfx)
{
	struct g_part_entry *iter;
	u_int idx;

	idx = 5;
	LIST_FOREACH(iter, &table->gpt_entry, gpe_entry) {
		if (iter == entry)
			break;
		idx++;
	}
	sbuf_printf(sb, "%.*s%u", (int)strlen(pfx) - 1, pfx, idx);
}
#endif

static int
g_part_ebr_modify(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_ebr_entry *entry;

	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		return (EINVAL);

	entry = (struct g_part_ebr_entry *)baseentry;
	if (gpp->gpp_parms & G_PART_PARM_TYPE)
		return (ebr_parse_type(gpp->gpp_type, &entry->ent.dp_typ));
	return (0);
}

static int
g_part_ebr_resize(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_provider *pp;

	if (baseentry != NULL)
		return (EOPNOTSUPP);
	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	basetable->gpt_last = MIN(pp->mediasize / pp->sectorsize,
	    UINT32_MAX) - 1;
	return (0);
}

static const char *
g_part_ebr_name(struct g_part_table *table, struct g_part_entry *entry,
    char *buf, size_t bufsz)
{

	snprintf(buf, bufsz, "+%08u", entry->gpe_index);
	return (buf);
}

static int
g_part_ebr_precheck(struct g_part_table *table, enum g_part_ctl req,
    struct g_part_parms *gpp)
{
#if defined(GEOM_PART_EBR_COMPAT)
	if (req == G_PART_CTL_DESTROY)
		return (0);
	return (ECANCELED);
#else
	/*
	 * The index is a function of the start of the partition.
	 * This is not something the user can override, nor is it
	 * something the common code will do right. We can set the
	 * index now so that we get what we need.
	 */
	if (req == G_PART_CTL_ADD)
		gpp->gpp_index = (gpp->gpp_start / table->gpt_sectors) + 1;
	return (0);
#endif
}

static int
g_part_ebr_probe(struct g_part_table *table, struct g_consumer *cp)
{
	char type[64];
	struct g_provider *pp;
	u_char *buf, *p;
	int error, index, res;
	uint16_t magic;

	pp = cp->provider;

	/* Sanity-check the provider. */
	if (pp->sectorsize < EBRSIZE || pp->mediasize < pp->sectorsize)
		return (ENOSPC);
	if (pp->sectorsize > 4096)
		return (ENXIO);

	/* Check that we have a parent and that it's a MBR. */
	if (table->gpt_depth == 0)
		return (ENXIO);
	error = g_getattr("PART::scheme", cp, &type);
	if (error != 0)
		return (error);
	if (strcmp(type, "MBR") != 0)
		return (ENXIO);
	/* Check that partition has type DOSPTYP_EBR. */
	error = g_getattr("PART::type", cp, &type);
	if (error != 0)
		return (error);
	if (strcmp(type, "ebr") != 0)
		return (ENXIO);

	/* Check that there's a EBR. */
	buf = g_read_data(cp, 0L, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);

	/* We goto out on mismatch. */
	res = ENXIO;

	magic = le16dec(buf + DOSMAGICOFFSET);
	if (magic != DOSMAGIC)
		goto out;

	for (index = 0; index < 2; index++) {
		p = buf + DOSPARTOFF + index * DOSPARTSIZE;
		if (p[0] != 0 && p[0] != 0x80)
			goto out;
	}
	res = G_PART_PROBE_PRI_NORM;

 out:
	g_free(buf);
	return (res);
}

static int
g_part_ebr_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct dos_partition ent[2];
	struct g_provider *pp;
	struct g_part_entry *baseentry;
	struct g_part_ebr_table *table;
	struct g_part_ebr_entry *entry;
	u_char *buf;
	off_t ofs, msize;
	u_int lba;
	int error, index;

	pp = cp->provider;
	table = (struct g_part_ebr_table *)basetable;
	msize = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);

	lba = 0;
	while (1) {
		ofs = (off_t)lba * pp->sectorsize;
		buf = g_read_data(cp, ofs, pp->sectorsize, &error);
		if (buf == NULL)
			return (error);

		ebr_entry_decode(buf + DOSPARTOFF + 0 * DOSPARTSIZE, ent + 0);
		ebr_entry_decode(buf + DOSPARTOFF + 1 * DOSPARTSIZE, ent + 1);

		/* The 3rd & 4th entries should be zeroes. */
		if (le64dec(buf + DOSPARTOFF + 2 * DOSPARTSIZE) +
		    le64dec(buf + DOSPARTOFF + 3 * DOSPARTSIZE) != 0) {
			basetable->gpt_corrupt = 1;
			printf("GEOM: %s: invalid entries in the EBR ignored.\n",
			    pp->name);
		}
#ifndef GEOM_PART_EBR_COMPAT
		/* Save the first EBR, it can contain a boot code */
		if (lba == 0)
			bcopy(buf, table->ebr, sizeof(table->ebr));
#endif
		g_free(buf);

		if (ent[0].dp_typ == 0)
			break;

		if (ent[0].dp_typ == 5 && ent[1].dp_typ == 0) {
			lba = ent[0].dp_start;
			continue;
		}

		index = (lba / basetable->gpt_sectors) + 1;
		baseentry = (struct g_part_entry *)g_part_new_entry(basetable,
		    index, lba, lba + ent[0].dp_start + ent[0].dp_size - 1);
		baseentry->gpe_offset = (off_t)(lba + ent[0].dp_start) *
		    pp->sectorsize;
		entry = (struct g_part_ebr_entry *)baseentry;
		entry->ent = ent[0];

		if (ent[1].dp_typ == 0)
			break;

		lba = ent[1].dp_start;
	}

	basetable->gpt_entries = msize / basetable->gpt_sectors;
	basetable->gpt_first = 0;
	basetable->gpt_last = msize - 1;
	return (0);
}

static int
g_part_ebr_setunset(struct g_part_table *table, struct g_part_entry *baseentry,
    const char *attrib, unsigned int set)
{
	struct g_part_entry *iter;
	struct g_part_ebr_entry *entry;
	int changed;

	if (baseentry == NULL)
		return (ENODEV);
	if (strcasecmp(attrib, "active") != 0)
		return (EINVAL);

	/* Only one entry can have the active attribute. */
	LIST_FOREACH(iter, &table->gpt_entry, gpe_entry) {
		if (iter->gpe_deleted)
			continue;
		changed = 0;
		entry = (struct g_part_ebr_entry *)iter;
		if (iter == baseentry) {
			if (set && (entry->ent.dp_flag & 0x80) == 0) {
				entry->ent.dp_flag |= 0x80;
				changed = 1;
			} else if (!set && (entry->ent.dp_flag & 0x80)) {
				entry->ent.dp_flag &= ~0x80;
				changed = 1;
			}
		} else {
			if (set && (entry->ent.dp_flag & 0x80)) {
				entry->ent.dp_flag &= ~0x80;
				changed = 1;
			}
		}
		if (changed && !iter->gpe_created)
			iter->gpe_modified = 1;
	}
	return (0);
}

static const char *
g_part_ebr_type(struct g_part_table *basetable, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_ebr_entry *entry;
	int i;

	entry = (struct g_part_ebr_entry *)baseentry;
	for (i = 0; i < nitems(ebr_alias_match); i++) {
		if (ebr_alias_match[i].typ == entry->ent.dp_typ)
			return (g_part_alias_name(ebr_alias_match[i].alias));
	}
	snprintf(buf, bufsz, "!%d", entry->ent.dp_typ);
	return (buf);
}

static int
g_part_ebr_write(struct g_part_table *basetable, struct g_consumer *cp)
{
#ifndef GEOM_PART_EBR_COMPAT
	struct g_part_ebr_table *table;
#endif
	struct g_provider *pp;
	struct g_part_entry *baseentry, *next;
	struct g_part_ebr_entry *entry;
	u_char *buf;
	u_char *p;
	int error;

	pp = cp->provider;
	buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
#ifndef GEOM_PART_EBR_COMPAT
	table = (struct g_part_ebr_table *)basetable;
	bcopy(table->ebr, buf, DOSPARTOFF);
#endif
	le16enc(buf + DOSMAGICOFFSET, DOSMAGIC);

	baseentry = LIST_FIRST(&basetable->gpt_entry);
	while (baseentry != NULL && baseentry->gpe_deleted)
		baseentry = LIST_NEXT(baseentry, gpe_entry);

	/* Wipe-out the first EBR when there are no slices. */
	if (baseentry == NULL) {
		error = g_write_data(cp, 0, buf, pp->sectorsize);
		goto out;
	}

	/*
	 * If the first partition is not in LBA 0, we need to
	 * put a "link" EBR in LBA 0.
	 */
	if (baseentry->gpe_start != 0) {
		ebr_entry_link(basetable, (uint32_t)baseentry->gpe_start,
		    (uint32_t)baseentry->gpe_end, buf + DOSPARTOFF);
		error = g_write_data(cp, 0, buf, pp->sectorsize);
		if (error)
			goto out;
	}

	do {
		entry = (struct g_part_ebr_entry *)baseentry;

		p = buf + DOSPARTOFF;
		p[0] = entry->ent.dp_flag;
		p[1] = entry->ent.dp_shd;
		p[2] = entry->ent.dp_ssect;
		p[3] = entry->ent.dp_scyl;
		p[4] = entry->ent.dp_typ;
		p[5] = entry->ent.dp_ehd;
		p[6] = entry->ent.dp_esect;
		p[7] = entry->ent.dp_ecyl;
		le32enc(p + 8, entry->ent.dp_start);
		le32enc(p + 12, entry->ent.dp_size);

		next = LIST_NEXT(baseentry, gpe_entry);
		while (next != NULL && next->gpe_deleted)
			next = LIST_NEXT(next, gpe_entry);

		p += DOSPARTSIZE;
		if (next != NULL)
			ebr_entry_link(basetable, (uint32_t)next->gpe_start,
			    (uint32_t)next->gpe_end, p);
		else
			bzero(p, DOSPARTSIZE);

		error = g_write_data(cp, baseentry->gpe_start * pp->sectorsize,
		    buf, pp->sectorsize);
#ifndef GEOM_PART_EBR_COMPAT
		if (baseentry->gpe_start == 0)
			bzero(buf, DOSPARTOFF);
#endif
		baseentry = next;
	} while (!error && baseentry != NULL);

 out:
	g_free(buf);
	return (error);
}
