/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2008 Marcel Moolenaar
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
#include <sys/apm.h>
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
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_apm, "GEOM partitioning class for Apple-style partitions");

struct g_part_apm_table {
	struct g_part_table	base;
	struct apm_ddr		ddr;
	struct apm_ent		self;
	int			tivo_series1;
};

struct g_part_apm_entry {
	struct g_part_entry	base;
	struct apm_ent		ent;
};

static int g_part_apm_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_apm_create(struct g_part_table *, struct g_part_parms *);
static int g_part_apm_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_apm_dumpconf(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
static int g_part_apm_dumpto(struct g_part_table *, struct g_part_entry *);
static int g_part_apm_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_apm_name(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_apm_probe(struct g_part_table *, struct g_consumer *);
static int g_part_apm_read(struct g_part_table *, struct g_consumer *);
static const char *g_part_apm_type(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_apm_write(struct g_part_table *, struct g_consumer *);
static int g_part_apm_resize(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);

static kobj_method_t g_part_apm_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_apm_add),
	KOBJMETHOD(g_part_create,	g_part_apm_create),
	KOBJMETHOD(g_part_destroy,	g_part_apm_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_apm_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_apm_dumpto),
	KOBJMETHOD(g_part_modify,	g_part_apm_modify),
	KOBJMETHOD(g_part_resize,	g_part_apm_resize),
	KOBJMETHOD(g_part_name,		g_part_apm_name),
	KOBJMETHOD(g_part_probe,	g_part_apm_probe),
	KOBJMETHOD(g_part_read,		g_part_apm_read),
	KOBJMETHOD(g_part_type,		g_part_apm_type),
	KOBJMETHOD(g_part_write,	g_part_apm_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_apm_scheme = {
	"APM",
	g_part_apm_methods,
	sizeof(struct g_part_apm_table),
	.gps_entrysz = sizeof(struct g_part_apm_entry),
	.gps_minent = 16,
	.gps_maxent = 4096,
};
G_PART_SCHEME_DECLARE(g_part_apm);
MODULE_VERSION(geom_part_apm, 0);

static void
swab(char *buf, size_t bufsz)
{
	int i;
	char ch;

	for (i = 0; i < bufsz; i += 2) {
		ch = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = ch;
	}
}

static int
apm_parse_type(const char *type, char *buf, size_t bufsz)
{
	const char *alias;

	if (type[0] == '!') {
		type++;
		if (strlen(type) > bufsz)
			return (EINVAL);
		if (!strcmp(type, APM_ENT_TYPE_SELF) ||
		    !strcmp(type, APM_ENT_TYPE_UNUSED))
			return (EINVAL);
		strncpy(buf, type, bufsz);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_APPLE_BOOT);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_APPLE_BOOT);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_APPLE_HFS);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_APPLE_HFS);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_APPLE_UFS);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_APPLE_UFS);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_NANDFS);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD_NANDFS);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_SWAP);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD_SWAP);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_UFS);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD_UFS);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_VINUM);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD_VINUM);
		return (0);
	}
	alias = g_part_alias_name(G_PART_ALIAS_FREEBSD_ZFS);
	if (!strcasecmp(type, alias)) {
		strcpy(buf, APM_ENT_TYPE_FREEBSD_ZFS);
		return (0);
	}
	return (EINVAL);
}

static int
apm_read_ent(struct g_consumer *cp, uint32_t blk, struct apm_ent *ent,
    int tivo_series1)
{
	struct g_provider *pp;
	char *buf;
	int error;

	pp = cp->provider;
	buf = g_read_data(cp, pp->sectorsize * blk, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);
	if (tivo_series1)
		swab(buf, pp->sectorsize);
	ent->ent_sig = be16dec(buf);
	ent->ent_pmblkcnt = be32dec(buf + 4);
	ent->ent_start = be32dec(buf + 8);
	ent->ent_size = be32dec(buf + 12);
	bcopy(buf + 16, ent->ent_name, sizeof(ent->ent_name));
	bcopy(buf + 48, ent->ent_type, sizeof(ent->ent_type));
	g_free(buf);
	return (0);
}

static int
g_part_apm_add(struct g_part_table *basetable, struct g_part_entry *baseentry,
    struct g_part_parms *gpp)
{
	struct g_part_apm_entry *entry;
	struct g_part_apm_table *table;
	int error;

	entry = (struct g_part_apm_entry *)baseentry;
	table = (struct g_part_apm_table *)basetable;
	entry->ent.ent_sig = APM_ENT_SIG;
	entry->ent.ent_pmblkcnt = table->self.ent_pmblkcnt;
	entry->ent.ent_start = gpp->gpp_start;
	entry->ent.ent_size = gpp->gpp_size;
	if (baseentry->gpe_deleted) {
		bzero(entry->ent.ent_type, sizeof(entry->ent.ent_type));
		bzero(entry->ent.ent_name, sizeof(entry->ent.ent_name));
	}
	error = apm_parse_type(gpp->gpp_type, entry->ent.ent_type,
	    sizeof(entry->ent.ent_type));
	if (error)
		return (error);
	if (gpp->gpp_parms & G_PART_PARM_LABEL) {
		if (strlen(gpp->gpp_label) > sizeof(entry->ent.ent_name))
			return (EINVAL);
		strncpy(entry->ent.ent_name, gpp->gpp_label,
		    sizeof(entry->ent.ent_name));
	}
	if (baseentry->gpe_index >= table->self.ent_pmblkcnt)
		table->self.ent_pmblkcnt = baseentry->gpe_index + 1;
	KASSERT(table->self.ent_size >= table->self.ent_pmblkcnt,
	    ("%s", __func__));
	KASSERT(table->self.ent_size > baseentry->gpe_index,
	    ("%s", __func__));
	return (0);
}

static int
g_part_apm_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_provider *pp;
	struct g_part_apm_table *table;
	uint32_t last;

	/* We don't nest, which means that our depth should be 0. */
	if (basetable->gpt_depth != 0)
		return (ENXIO);

	table = (struct g_part_apm_table *)basetable;
	pp = gpp->gpp_provider;
	if (pp->sectorsize != 512 ||
	    pp->mediasize < (2 + 2 * basetable->gpt_entries) * pp->sectorsize)
		return (ENOSPC);

	/* APM uses 32-bit LBAs. */
	last = MIN(pp->mediasize / pp->sectorsize, UINT32_MAX) - 1;

	basetable->gpt_first = 2 + basetable->gpt_entries;
	basetable->gpt_last = last;

	table->ddr.ddr_sig = APM_DDR_SIG;
	table->ddr.ddr_blksize = pp->sectorsize;
	table->ddr.ddr_blkcount = last + 1;

	table->self.ent_sig = APM_ENT_SIG;
	table->self.ent_pmblkcnt = basetable->gpt_entries + 1;
	table->self.ent_start = 1;
	table->self.ent_size = table->self.ent_pmblkcnt;
	strcpy(table->self.ent_name, "Apple");
	strcpy(table->self.ent_type, APM_ENT_TYPE_SELF);
	return (0);
}

static int
g_part_apm_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	/* Wipe the first 2 sectors to clear the partitioning. */
	basetable->gpt_smhead |= 3;
	return (0);
}

static void
g_part_apm_dumpconf(struct g_part_table *table, struct g_part_entry *baseentry,
    struct sbuf *sb, const char *indent)
{
	union {
		char name[APM_ENT_NAMELEN + 1];
		char type[APM_ENT_TYPELEN + 1];
	} u;
	struct g_part_apm_entry *entry;

	entry = (struct g_part_apm_entry *)baseentry;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs APPLE xt %s", entry->ent.ent_type);
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		strncpy(u.name, entry->ent.ent_name, APM_ENT_NAMELEN);
		u.name[APM_ENT_NAMELEN] = '\0';
		sbuf_printf(sb, "%s<label>", indent);
		g_conf_printf_escaped(sb, "%s", u.name);
		sbuf_printf(sb, "</label>\n");
		strncpy(u.type, entry->ent.ent_type, APM_ENT_TYPELEN);
		u.type[APM_ENT_TYPELEN] = '\0';
		sbuf_printf(sb, "%s<rawtype>", indent);
		g_conf_printf_escaped(sb, "%s", u.type);
		sbuf_printf(sb, "</rawtype>\n");
	} else {
		/* confxml: scheme information */
	}
}

static int
g_part_apm_dumpto(struct g_part_table *table, struct g_part_entry *baseentry)
{
	struct g_part_apm_entry *entry;

	entry = (struct g_part_apm_entry *)baseentry;
	return ((!strcmp(entry->ent.ent_type, APM_ENT_TYPE_FREEBSD_SWAP))
	    ? 1 : 0);
}

static int
g_part_apm_modify(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_apm_entry *entry;
	int error;

	entry = (struct g_part_apm_entry *)baseentry;
	if (gpp->gpp_parms & G_PART_PARM_LABEL) {
		if (strlen(gpp->gpp_label) > sizeof(entry->ent.ent_name))
			return (EINVAL);
	}
	if (gpp->gpp_parms & G_PART_PARM_TYPE) {
		error = apm_parse_type(gpp->gpp_type, entry->ent.ent_type,
		    sizeof(entry->ent.ent_type));
		if (error)
			return (error);
	}
	if (gpp->gpp_parms & G_PART_PARM_LABEL) {
		strncpy(entry->ent.ent_name, gpp->gpp_label,
		    sizeof(entry->ent.ent_name));
	}
	return (0);
}

static int
g_part_apm_resize(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_apm_entry *entry;
	struct g_provider *pp;

	if (baseentry == NULL) {
		pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
		basetable->gpt_last = MIN(pp->mediasize / pp->sectorsize,
		    UINT32_MAX) - 1;
		return (0);
	}

	entry = (struct g_part_apm_entry *)baseentry;
	baseentry->gpe_end = baseentry->gpe_start + gpp->gpp_size - 1;
	entry->ent.ent_size = gpp->gpp_size;

	return (0);
}

static const char *
g_part_apm_name(struct g_part_table *table, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{

	snprintf(buf, bufsz, "s%d", baseentry->gpe_index + 1);
	return (buf);
}

static int
g_part_apm_probe(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_apm_table *table;
	char *buf;
	int error;

	/* We don't nest, which means that our depth should be 0. */
	if (basetable->gpt_depth != 0)
		return (ENXIO);

	table = (struct g_part_apm_table *)basetable;
	table->tivo_series1 = 0;
	pp = cp->provider;

	/* Sanity-check the provider. */
	if (pp->mediasize < 4 * pp->sectorsize)
		return (ENOSPC);

	/* Check that there's a Driver Descriptor Record (DDR). */
	buf = g_read_data(cp, 0L, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);
	if (be16dec(buf) == APM_DDR_SIG) {
		/* Normal Apple DDR */
		table->ddr.ddr_sig = be16dec(buf);
		table->ddr.ddr_blksize = be16dec(buf + 2);
		table->ddr.ddr_blkcount = be32dec(buf + 4);
		g_free(buf);
		if (table->ddr.ddr_blksize != pp->sectorsize)
			return (ENXIO);
		if (table->ddr.ddr_blkcount > pp->mediasize / pp->sectorsize)
			return (ENXIO);
	} else {
		/*
		 * Check for Tivo drives, which have no DDR and a different
		 * signature.  Those whose first two bytes are 14 92 are
		 * Series 2 drives, and aren't supported.  Those that start
		 * with 92 14 are series 1 drives and are supported.
		 */
		if (be16dec(buf) != 0x9214) {
			/* If this is 0x1492 it could be a series 2 drive */
			g_free(buf);
			return (ENXIO);
		}
		table->ddr.ddr_sig = APM_DDR_SIG;		/* XXX */
		table->ddr.ddr_blksize = pp->sectorsize;	/* XXX */
		table->ddr.ddr_blkcount =
		    MIN(pp->mediasize / pp->sectorsize, UINT32_MAX);
		table->tivo_series1 = 1;
		g_free(buf);
	}

	/* Check that there's a Partition Map. */
	error = apm_read_ent(cp, 1, &table->self, table->tivo_series1);
	if (error)
		return (error);
	if (table->self.ent_sig != APM_ENT_SIG)
		return (ENXIO);
	if (strcmp(table->self.ent_type, APM_ENT_TYPE_SELF))
		return (ENXIO);
	if (table->self.ent_pmblkcnt >= table->ddr.ddr_blkcount)
		return (ENXIO);
	return (G_PART_PROBE_PRI_NORM);
}

static int
g_part_apm_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct apm_ent ent;
	struct g_part_apm_entry *entry;
	struct g_part_apm_table *table;
	int error, index;

	table = (struct g_part_apm_table *)basetable;

	basetable->gpt_first = table->self.ent_size + 1;
	basetable->gpt_last = table->ddr.ddr_blkcount - 1;
	basetable->gpt_entries = table->self.ent_size - 1;

	for (index = table->self.ent_pmblkcnt - 1; index > 0; index--) {
		error = apm_read_ent(cp, index + 1, &ent, table->tivo_series1);
		if (error)
			continue;
		if (!strcmp(ent.ent_type, APM_ENT_TYPE_UNUSED))
			continue;
		entry = (struct g_part_apm_entry *)g_part_new_entry(basetable,
		    index, ent.ent_start, ent.ent_start + ent.ent_size - 1);
		entry->ent = ent;
	}

	return (0);
}

static const char *
g_part_apm_type(struct g_part_table *basetable, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_apm_entry *entry;
	const char *type;
	size_t len;

	entry = (struct g_part_apm_entry *)baseentry;
	type = entry->ent.ent_type;
	if (!strcmp(type, APM_ENT_TYPE_APPLE_BOOT))
		return (g_part_alias_name(G_PART_ALIAS_APPLE_BOOT));
	if (!strcmp(type, APM_ENT_TYPE_APPLE_HFS))
		return (g_part_alias_name(G_PART_ALIAS_APPLE_HFS));
	if (!strcmp(type, APM_ENT_TYPE_APPLE_UFS))
		return (g_part_alias_name(G_PART_ALIAS_APPLE_UFS));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD_NANDFS))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_NANDFS));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD_SWAP))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_SWAP));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD_UFS))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_UFS));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD_VINUM))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_VINUM));
	if (!strcmp(type, APM_ENT_TYPE_FREEBSD_ZFS))
		return (g_part_alias_name(G_PART_ALIAS_FREEBSD_ZFS));
	buf[0] = '!';
	len = MIN(sizeof(entry->ent.ent_type), bufsz - 2);
	bcopy(type, buf + 1, len);
	buf[len + 1] = '\0';
	return (buf);
}

static int
g_part_apm_write(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_entry *baseentry;
	struct g_part_apm_entry *entry;
	struct g_part_apm_table *table;
	char *buf, *ptr;
	uint32_t index;
	int error;
	size_t tblsz;

	pp = cp->provider;
	table = (struct g_part_apm_table *)basetable;
	/*
	 * Tivo Series 1 disk partitions are currently read-only.
	 */
	if (table->tivo_series1)
		return (EOPNOTSUPP);

	/* Write the DDR only when we're newly created. */
	if (basetable->gpt_created) {
		buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
		be16enc(buf, table->ddr.ddr_sig);
		be16enc(buf + 2, table->ddr.ddr_blksize);
		be32enc(buf + 4, table->ddr.ddr_blkcount);
		error = g_write_data(cp, 0, buf, pp->sectorsize);
		g_free(buf);
		if (error)
			return (error);
	}

	/* Allocate the buffer for all entries */
	tblsz = table->self.ent_pmblkcnt;
	buf = g_malloc(tblsz * pp->sectorsize, M_WAITOK | M_ZERO);

	/* Fill the self entry */
	be16enc(buf, APM_ENT_SIG);
	be32enc(buf + 4, table->self.ent_pmblkcnt);
	be32enc(buf + 8, table->self.ent_start);
	be32enc(buf + 12, table->self.ent_size);
	bcopy(table->self.ent_name, buf + 16, sizeof(table->self.ent_name));
	bcopy(table->self.ent_type, buf + 48, sizeof(table->self.ent_type));

	baseentry = LIST_FIRST(&basetable->gpt_entry);
	for (index = 1; index < tblsz; index++) {
		entry = (baseentry != NULL && index == baseentry->gpe_index)
		    ? (struct g_part_apm_entry *)baseentry : NULL;
		ptr = buf + index * pp->sectorsize;
		be16enc(ptr, APM_ENT_SIG);
		be32enc(ptr + 4, table->self.ent_pmblkcnt);
		if (entry != NULL && !baseentry->gpe_deleted) {
			be32enc(ptr + 8, entry->ent.ent_start);
			be32enc(ptr + 12, entry->ent.ent_size);
			bcopy(entry->ent.ent_name, ptr + 16,
			    sizeof(entry->ent.ent_name));
			bcopy(entry->ent.ent_type, ptr + 48,
			    sizeof(entry->ent.ent_type));
		} else {
			strcpy(ptr + 48, APM_ENT_TYPE_UNUSED);
		}
		if (entry != NULL)
			baseentry = LIST_NEXT(baseentry, gpe_entry);
	}

	for (index = 0; index < tblsz; index += MAXPHYS / pp->sectorsize) {
		error = g_write_data(cp, (1 + index) * pp->sectorsize,
		    buf + index * pp->sectorsize,
		    (tblsz - index > MAXPHYS / pp->sectorsize) ? MAXPHYS:
		    (tblsz - index) * pp->sectorsize);
		if (error) {
			g_free(buf);
			return (error);
		}
	}
	g_free(buf);
	return (0);
}
