/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2005-2009 Marcel Moolenaar
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
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uuid.h>
#include <geom/geom.h>
#include <geom/geom_ctl.h>
#include <geom/geom_int.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

#ifndef _PATH_DEV
#define _PATH_DEV "/dev/"
#endif

static kobj_method_t g_part_null_methods[] = {
	{ 0, 0 }
};

static struct g_part_scheme g_part_null_scheme = {
	"(none)",
	g_part_null_methods,
	sizeof(struct g_part_table),
};

TAILQ_HEAD(, g_part_scheme) g_part_schemes =
    TAILQ_HEAD_INITIALIZER(g_part_schemes);

struct g_part_alias_list {
	const char *lexeme;
	enum g_part_alias alias;
} g_part_alias_list[G_PART_ALIAS_COUNT] = {
	{ "apple-apfs", G_PART_ALIAS_APPLE_APFS },
	{ "apple-boot", G_PART_ALIAS_APPLE_BOOT },
	{ "apple-core-storage", G_PART_ALIAS_APPLE_CORE_STORAGE },
	{ "apple-hfs", G_PART_ALIAS_APPLE_HFS },
	{ "apple-label", G_PART_ALIAS_APPLE_LABEL },
	{ "apple-raid", G_PART_ALIAS_APPLE_RAID },
	{ "apple-raid-offline", G_PART_ALIAS_APPLE_RAID_OFFLINE },
	{ "apple-tv-recovery", G_PART_ALIAS_APPLE_TV_RECOVERY },
	{ "apple-ufs", G_PART_ALIAS_APPLE_UFS },
	{ "bios-boot", G_PART_ALIAS_BIOS_BOOT },
	{ "chromeos-firmware", G_PART_ALIAS_CHROMEOS_FIRMWARE },
	{ "chromeos-kernel", G_PART_ALIAS_CHROMEOS_KERNEL },
	{ "chromeos-reserved", G_PART_ALIAS_CHROMEOS_RESERVED },
	{ "chromeos-root", G_PART_ALIAS_CHROMEOS_ROOT },
	{ "dragonfly-ccd", G_PART_ALIAS_DFBSD_CCD },
	{ "dragonfly-hammer", G_PART_ALIAS_DFBSD_HAMMER },
	{ "dragonfly-hammer2", G_PART_ALIAS_DFBSD_HAMMER2 },
	{ "dragonfly-label32", G_PART_ALIAS_DFBSD },
	{ "dragonfly-label64", G_PART_ALIAS_DFBSD64 },
	{ "dragonfly-legacy", G_PART_ALIAS_DFBSD_LEGACY },
	{ "dragonfly-swap", G_PART_ALIAS_DFBSD_SWAP },
	{ "dragonfly-ufs", G_PART_ALIAS_DFBSD_UFS },
	{ "dragonfly-vinum", G_PART_ALIAS_DFBSD_VINUM },
	{ "ebr", G_PART_ALIAS_EBR },
	{ "efi", G_PART_ALIAS_EFI },
	{ "fat16", G_PART_ALIAS_MS_FAT16 },
	{ "fat32", G_PART_ALIAS_MS_FAT32 },
	{ "fat32lba", G_PART_ALIAS_MS_FAT32LBA },
	{ "freebsd", G_PART_ALIAS_FREEBSD },
	{ "freebsd-boot", G_PART_ALIAS_FREEBSD_BOOT },
	{ "freebsd-nandfs", G_PART_ALIAS_FREEBSD_NANDFS },
	{ "freebsd-swap", G_PART_ALIAS_FREEBSD_SWAP },
	{ "freebsd-ufs", G_PART_ALIAS_FREEBSD_UFS },
	{ "freebsd-vinum", G_PART_ALIAS_FREEBSD_VINUM },
	{ "freebsd-zfs", G_PART_ALIAS_FREEBSD_ZFS },
	{ "linux-data", G_PART_ALIAS_LINUX_DATA },
	{ "linux-lvm", G_PART_ALIAS_LINUX_LVM },
	{ "linux-raid", G_PART_ALIAS_LINUX_RAID },
	{ "linux-swap", G_PART_ALIAS_LINUX_SWAP },
	{ "mbr", G_PART_ALIAS_MBR },
	{ "ms-basic-data", G_PART_ALIAS_MS_BASIC_DATA },
	{ "ms-ldm-data", G_PART_ALIAS_MS_LDM_DATA },
	{ "ms-ldm-metadata", G_PART_ALIAS_MS_LDM_METADATA },
	{ "ms-recovery", G_PART_ALIAS_MS_RECOVERY },
	{ "ms-reserved", G_PART_ALIAS_MS_RESERVED },
	{ "ms-spaces", G_PART_ALIAS_MS_SPACES },
	{ "netbsd-ccd", G_PART_ALIAS_NETBSD_CCD },
	{ "netbsd-cgd", G_PART_ALIAS_NETBSD_CGD },
	{ "netbsd-ffs", G_PART_ALIAS_NETBSD_FFS },
	{ "netbsd-lfs", G_PART_ALIAS_NETBSD_LFS },
	{ "netbsd-raid", G_PART_ALIAS_NETBSD_RAID },
	{ "netbsd-swap", G_PART_ALIAS_NETBSD_SWAP },
	{ "ntfs", G_PART_ALIAS_MS_NTFS },
	{ "openbsd-data", G_PART_ALIAS_OPENBSD_DATA },
	{ "prep-boot", G_PART_ALIAS_PREP_BOOT },
	{ "vmware-reserved", G_PART_ALIAS_VMRESERVED },
	{ "vmware-vmfs", G_PART_ALIAS_VMFS },
	{ "vmware-vmkdiag", G_PART_ALIAS_VMKDIAG },
	{ "vmware-vsanhdr", G_PART_ALIAS_VMVSANHDR },
};

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, part, CTLFLAG_RW, 0,
    "GEOM_PART stuff");
static u_int check_integrity = 1;
SYSCTL_UINT(_kern_geom_part, OID_AUTO, check_integrity,
    CTLFLAG_RWTUN, &check_integrity, 1,
    "Enable integrity checking");
static u_int auto_resize = 1;
SYSCTL_UINT(_kern_geom_part, OID_AUTO, auto_resize,
    CTLFLAG_RWTUN, &auto_resize, 1,
    "Enable auto resize");

/*
 * The GEOM partitioning class.
 */
static g_ctl_req_t g_part_ctlreq;
static g_ctl_destroy_geom_t g_part_destroy_geom;
static g_fini_t g_part_fini;
static g_init_t g_part_init;
static g_taste_t g_part_taste;

static g_access_t g_part_access;
static g_dumpconf_t g_part_dumpconf;
static g_orphan_t g_part_orphan;
static g_spoiled_t g_part_spoiled;
static g_start_t g_part_start;
static g_resize_t g_part_resize;
static g_ioctl_t g_part_ioctl;

static struct g_class g_part_class = {
	.name = "PART",
	.version = G_VERSION,
	/* Class methods. */
	.ctlreq = g_part_ctlreq,
	.destroy_geom = g_part_destroy_geom,
	.fini = g_part_fini,
	.init = g_part_init,
	.taste = g_part_taste,
	/* Geom methods. */
	.access = g_part_access,
	.dumpconf = g_part_dumpconf,
	.orphan = g_part_orphan,
	.spoiled = g_part_spoiled,
	.start = g_part_start,
	.resize = g_part_resize,
	.ioctl = g_part_ioctl,
};

DECLARE_GEOM_CLASS(g_part_class, g_part);
MODULE_VERSION(g_part, 0);

/*
 * Support functions.
 */

static void g_part_wither(struct g_geom *, int);

const char *
g_part_alias_name(enum g_part_alias alias)
{
	int i;

	for (i = 0; i < G_PART_ALIAS_COUNT; i++) {
		if (g_part_alias_list[i].alias != alias)
			continue;
		return (g_part_alias_list[i].lexeme);
	}

	return (NULL);
}

void
g_part_geometry_heads(off_t blocks, u_int sectors, off_t *bestchs,
    u_int *bestheads)
{
	static u_int candidate_heads[] = { 1, 2, 16, 32, 64, 128, 255, 0 };
	off_t chs, cylinders;
	u_int heads;
	int idx;

	*bestchs = 0;
	*bestheads = 0;
	for (idx = 0; candidate_heads[idx] != 0; idx++) {
		heads = candidate_heads[idx];
		cylinders = blocks / heads / sectors;
		if (cylinders < heads || cylinders < sectors)
			break;
		if (cylinders > 1023)
			continue;
		chs = cylinders * heads * sectors;
		if (chs > *bestchs || (chs == *bestchs && *bestheads == 1)) {
			*bestchs = chs;
			*bestheads = heads;
		}
	}
}

static void
g_part_geometry(struct g_part_table *table, struct g_consumer *cp,
    off_t blocks)
{
	static u_int candidate_sectors[] = { 1, 9, 17, 33, 63, 0 };
	off_t chs, bestchs;
	u_int heads, sectors;
	int idx;

	if (g_getattr("GEOM::fwsectors", cp, &sectors) != 0 || sectors == 0 ||
	    g_getattr("GEOM::fwheads", cp, &heads) != 0 || heads == 0) {
		table->gpt_fixgeom = 0;
		table->gpt_heads = 0;
		table->gpt_sectors = 0;
		bestchs = 0;
		for (idx = 0; candidate_sectors[idx] != 0; idx++) {
			sectors = candidate_sectors[idx];
			g_part_geometry_heads(blocks, sectors, &chs, &heads);
			if (chs == 0)
				continue;
			/*
			 * Prefer a geometry with sectors > 1, but only if
			 * it doesn't bump down the number of heads to 1.
			 */
			if (chs > bestchs || (chs == bestchs && heads > 1 &&
			    table->gpt_sectors == 1)) {
				bestchs = chs;
				table->gpt_heads = heads;
				table->gpt_sectors = sectors;
			}
		}
		/*
		 * If we didn't find a geometry at all, then the disk is
		 * too big. This means we can use the maximum number of
		 * heads and sectors.
		 */
		if (bestchs == 0) {
			table->gpt_heads = 255;
			table->gpt_sectors = 63;
		}
	} else {
		table->gpt_fixgeom = 1;
		table->gpt_heads = heads;
		table->gpt_sectors = sectors;
	}
}

static void
g_part_get_physpath_done(struct bio *bp)
{
	struct g_geom *gp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct g_provider *pp;
	struct bio *pbp;

	pbp = bp->bio_parent;
	pp = pbp->bio_to;
	gp = pp->geom;
	table = gp->softc;
	entry = pp->private;

	if (bp->bio_error == 0) {
		char *end;
		size_t len, remainder;
		len = strlcat(bp->bio_data, "/", bp->bio_length);
		if (len < bp->bio_length) {
			end = bp->bio_data + len;
			remainder = bp->bio_length - len;
			G_PART_NAME(table, entry, end, remainder);
		}
	}
	g_std_done(bp);
}


#define	DPRINTF(...)	if (bootverbose) {	\
	printf("GEOM_PART: " __VA_ARGS__);	\
}

static int
g_part_check_integrity(struct g_part_table *table, struct g_consumer *cp)
{
	struct g_part_entry *e1, *e2;
	struct g_provider *pp;
	off_t offset;
	int failed;

	failed = 0;
	pp = cp->provider;
	if (table->gpt_last < table->gpt_first) {
		DPRINTF("last LBA is below first LBA: %jd < %jd\n",
		    (intmax_t)table->gpt_last, (intmax_t)table->gpt_first);
		failed++;
	}
	if (table->gpt_last > pp->mediasize / pp->sectorsize - 1) {
		DPRINTF("last LBA extends beyond mediasize: "
		    "%jd > %jd\n", (intmax_t)table->gpt_last,
		    (intmax_t)pp->mediasize / pp->sectorsize - 1);
		failed++;
	}
	LIST_FOREACH(e1, &table->gpt_entry, gpe_entry) {
		if (e1->gpe_deleted || e1->gpe_internal)
			continue;
		if (e1->gpe_start < table->gpt_first) {
			DPRINTF("partition %d has start offset below first "
			    "LBA: %jd < %jd\n", e1->gpe_index,
			    (intmax_t)e1->gpe_start,
			    (intmax_t)table->gpt_first);
			failed++;
		}
		if (e1->gpe_start > table->gpt_last) {
			DPRINTF("partition %d has start offset beyond last "
			    "LBA: %jd > %jd\n", e1->gpe_index,
			    (intmax_t)e1->gpe_start,
			    (intmax_t)table->gpt_last);
			failed++;
		}
		if (e1->gpe_end < e1->gpe_start) {
			DPRINTF("partition %d has end offset below start "
			    "offset: %jd < %jd\n", e1->gpe_index,
			    (intmax_t)e1->gpe_end,
			    (intmax_t)e1->gpe_start);
			failed++;
		}
		if (e1->gpe_end > table->gpt_last) {
			DPRINTF("partition %d has end offset beyond last "
			    "LBA: %jd > %jd\n", e1->gpe_index,
			    (intmax_t)e1->gpe_end,
			    (intmax_t)table->gpt_last);
			failed++;
		}
		if (pp->stripesize > 0) {
			offset = e1->gpe_start * pp->sectorsize;
			if (e1->gpe_offset > offset)
				offset = e1->gpe_offset;
			if ((offset + pp->stripeoffset) % pp->stripesize) {
				DPRINTF("partition %d on (%s, %s) is not "
				    "aligned on %ju bytes\n", e1->gpe_index,
				    pp->name, table->gpt_scheme->name,
				    (uintmax_t)pp->stripesize);
				/* Don't treat this as a critical failure */
			}
		}
		e2 = e1;
		while ((e2 = LIST_NEXT(e2, gpe_entry)) != NULL) {
			if (e2->gpe_deleted || e2->gpe_internal)
				continue;
			if (e1->gpe_start >= e2->gpe_start &&
			    e1->gpe_start <= e2->gpe_end) {
				DPRINTF("partition %d has start offset inside "
				    "partition %d: start[%d] %jd >= start[%d] "
				    "%jd <= end[%d] %jd\n",
				    e1->gpe_index, e2->gpe_index,
				    e2->gpe_index, (intmax_t)e2->gpe_start,
				    e1->gpe_index, (intmax_t)e1->gpe_start,
				    e2->gpe_index, (intmax_t)e2->gpe_end);
				failed++;
			}
			if (e1->gpe_end >= e2->gpe_start &&
			    e1->gpe_end <= e2->gpe_end) {
				DPRINTF("partition %d has end offset inside "
				    "partition %d: start[%d] %jd >= end[%d] "
				    "%jd <= end[%d] %jd\n",
				    e1->gpe_index, e2->gpe_index,
				    e2->gpe_index, (intmax_t)e2->gpe_start,
				    e1->gpe_index, (intmax_t)e1->gpe_end,
				    e2->gpe_index, (intmax_t)e2->gpe_end);
				failed++;
			}
			if (e1->gpe_start < e2->gpe_start &&
			    e1->gpe_end > e2->gpe_end) {
				DPRINTF("partition %d contains partition %d: "
				    "start[%d] %jd > start[%d] %jd, end[%d] "
				    "%jd < end[%d] %jd\n",
				    e1->gpe_index, e2->gpe_index,
				    e1->gpe_index, (intmax_t)e1->gpe_start,
				    e2->gpe_index, (intmax_t)e2->gpe_start,
				    e2->gpe_index, (intmax_t)e2->gpe_end,
				    e1->gpe_index, (intmax_t)e1->gpe_end);
				failed++;
			}
		}
	}
	if (failed != 0) {
		printf("GEOM_PART: integrity check failed (%s, %s)\n",
		    pp->name, table->gpt_scheme->name);
		if (check_integrity != 0)
			return (EINVAL);
		table->gpt_corrupt = 1;
	}
	return (0);
}
#undef	DPRINTF

struct g_part_entry *
g_part_new_entry(struct g_part_table *table, int index, quad_t start,
    quad_t end)
{
	struct g_part_entry *entry, *last;

	last = NULL;
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_index == index)
			break;
		if (entry->gpe_index > index) {
			entry = NULL;
			break;
		}
		last = entry;
	}
	if (entry == NULL) {
		entry = g_malloc(table->gpt_scheme->gps_entrysz,
		    M_WAITOK | M_ZERO);
		entry->gpe_index = index;
		if (last == NULL)
			LIST_INSERT_HEAD(&table->gpt_entry, entry, gpe_entry);
		else
			LIST_INSERT_AFTER(last, entry, gpe_entry);
	} else
		entry->gpe_offset = 0;
	entry->gpe_start = start;
	entry->gpe_end = end;
	return (entry);
}

static void
g_part_new_provider(struct g_geom *gp, struct g_part_table *table,
    struct g_part_entry *entry)
{
	struct g_consumer *cp;
	struct g_provider *pp;
	struct sbuf *sb;
	struct g_geom_alias *gap;
	off_t offset;

	cp = LIST_FIRST(&gp->consumer);
	pp = cp->provider;

	offset = entry->gpe_start * pp->sectorsize;
	if (entry->gpe_offset < offset)
		entry->gpe_offset = offset;

	if (entry->gpe_pp == NULL) {
		/*
		 * Add aliases to the geom before we create the provider so that
		 * geom_dev can taste it with all the aliases in place so all
		 * the aliased dev_t instances get created for each partition
		 * (eg foo5p7 gets created for bar5p7 when foo is an alias of bar).
		 */
		LIST_FOREACH(gap, &table->gpt_gp->aliases, ga_next) {
			sb = sbuf_new_auto();
			G_PART_FULLNAME(table, entry, sb, gap->ga_alias);
			sbuf_finish(sb);
			g_geom_add_alias(gp, sbuf_data(sb));
			sbuf_delete(sb);
		}
		sb = sbuf_new_auto();
		G_PART_FULLNAME(table, entry, sb, gp->name);
		sbuf_finish(sb);
		entry->gpe_pp = g_new_providerf(gp, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		entry->gpe_pp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
		entry->gpe_pp->private = entry;		/* Close the circle. */
	}
	entry->gpe_pp->index = entry->gpe_index - 1;	/* index is 1-based. */
	entry->gpe_pp->mediasize = (entry->gpe_end - entry->gpe_start + 1) *
	    pp->sectorsize;
	entry->gpe_pp->mediasize -= entry->gpe_offset - offset;
	entry->gpe_pp->sectorsize = pp->sectorsize;
	entry->gpe_pp->stripesize = pp->stripesize;
	entry->gpe_pp->stripeoffset = pp->stripeoffset + entry->gpe_offset;
	if (pp->stripesize > 0)
		entry->gpe_pp->stripeoffset %= pp->stripesize;
	entry->gpe_pp->flags |= pp->flags & G_PF_ACCEPT_UNMAPPED;
	g_error_provider(entry->gpe_pp, 0);
}

static struct g_geom*
g_part_find_geom(const char *name)
{
	struct g_geom *gp;
	LIST_FOREACH(gp, &g_part_class.geom, geom) {
		if ((gp->flags & G_GEOM_WITHER) == 0 &&
		    strcmp(name, gp->name) == 0)
			break;
	}
	return (gp);
}

static int
g_part_parm_geom(struct gctl_req *req, const char *name, struct g_geom **v)
{
	struct g_geom *gp;
	const char *gname;

	gname = gctl_get_asciiparam(req, name);
	if (gname == NULL)
		return (ENOATTR);
	if (strncmp(gname, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		gname += sizeof(_PATH_DEV) - 1;
	gp = g_part_find_geom(gname);
	if (gp == NULL) {
		gctl_error(req, "%d %s '%s'", EINVAL, name, gname);
		return (EINVAL);
	}
	*v = gp;
	return (0);
}

static int
g_part_parm_provider(struct gctl_req *req, const char *name,
    struct g_provider **v)
{
	struct g_provider *pp;
	const char *pname;

	pname = gctl_get_asciiparam(req, name);
	if (pname == NULL)
		return (ENOATTR);
	if (strncmp(pname, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		pname += sizeof(_PATH_DEV) - 1;
	pp = g_provider_by_name(pname);
	if (pp == NULL) {
		gctl_error(req, "%d %s '%s'", EINVAL, name, pname);
		return (EINVAL);
	}
	*v = pp;
	return (0);
}

static int
g_part_parm_quad(struct gctl_req *req, const char *name, quad_t *v)
{
	const char *p;
	char *x;
	quad_t q;

	p = gctl_get_asciiparam(req, name);
	if (p == NULL)
		return (ENOATTR);
	q = strtoq(p, &x, 0);
	if (*x != '\0' || q < 0) {
		gctl_error(req, "%d %s '%s'", EINVAL, name, p);
		return (EINVAL);
	}
	*v = q;
	return (0);
}

static int
g_part_parm_scheme(struct gctl_req *req, const char *name,
    struct g_part_scheme **v)
{
	struct g_part_scheme *s;
	const char *p;

	p = gctl_get_asciiparam(req, name);
	if (p == NULL)
		return (ENOATTR);
	TAILQ_FOREACH(s, &g_part_schemes, scheme_list) {
		if (s == &g_part_null_scheme)
			continue;
		if (!strcasecmp(s->name, p))
			break;
	}
	if (s == NULL) {
		gctl_error(req, "%d %s '%s'", EINVAL, name, p);
		return (EINVAL);
	}
	*v = s;
	return (0);
}

static int
g_part_parm_str(struct gctl_req *req, const char *name, const char **v)
{
	const char *p;

	p = gctl_get_asciiparam(req, name);
	if (p == NULL)
		return (ENOATTR);
	/* An empty label is always valid. */
	if (strcmp(name, "label") != 0 && p[0] == '\0') {
		gctl_error(req, "%d %s '%s'", EINVAL, name, p);
		return (EINVAL);
	}
	*v = p;
	return (0);
}

static int
g_part_parm_intmax(struct gctl_req *req, const char *name, u_int *v)
{
	const intmax_t *p;
	int size;

	p = gctl_get_param(req, name, &size);
	if (p == NULL)
		return (ENOATTR);
	if (size != sizeof(*p) || *p < 0 || *p > INT_MAX) {
		gctl_error(req, "%d %s '%jd'", EINVAL, name, *p);
		return (EINVAL);
	}
	*v = (u_int)*p;
	return (0);
}

static int
g_part_parm_uint32(struct gctl_req *req, const char *name, u_int *v)
{
	const uint32_t *p;
	int size;

	p = gctl_get_param(req, name, &size);
	if (p == NULL)
		return (ENOATTR);
	if (size != sizeof(*p) || *p > INT_MAX) {
		gctl_error(req, "%d %s '%u'", EINVAL, name, (unsigned int)*p);
		return (EINVAL);
	}
	*v = (u_int)*p;
	return (0);
}

static int
g_part_parm_bootcode(struct gctl_req *req, const char *name, const void **v,
    unsigned int *s)
{
	const void *p;
	int size;

	p = gctl_get_param(req, name, &size);
	if (p == NULL)
		return (ENOATTR);
	*v = p;
	*s = size;
	return (0);
}

static int
g_part_probe(struct g_geom *gp, struct g_consumer *cp, int depth)
{
	struct g_part_scheme *iter, *scheme;
	struct g_part_table *table;
	int pri, probe;

	table = gp->softc;
	scheme = (table != NULL) ? table->gpt_scheme : NULL;
	pri = (scheme != NULL) ? G_PART_PROBE(table, cp) : INT_MIN;
	if (pri == 0)
		goto done;
	if (pri > 0) {	/* error */
		scheme = NULL;
		pri = INT_MIN;
	}

	TAILQ_FOREACH(iter, &g_part_schemes, scheme_list) {
		if (iter == &g_part_null_scheme)
			continue;
		table = (void *)kobj_create((kobj_class_t)iter, M_GEOM,
		    M_WAITOK);
		table->gpt_gp = gp;
		table->gpt_scheme = iter;
		table->gpt_depth = depth;
		probe = G_PART_PROBE(table, cp);
		if (probe <= 0 && probe > pri) {
			pri = probe;
			scheme = iter;
			if (gp->softc != NULL)
				kobj_delete((kobj_t)gp->softc, M_GEOM);
			gp->softc = table;
			if (pri == 0)
				goto done;
		} else
			kobj_delete((kobj_t)table, M_GEOM);
	}

done:
	return ((scheme == NULL) ? ENXIO : 0);
}

/*
 * Control request functions.
 */

static int
g_part_ctl_add(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_entry *delent, *last, *entry;
	struct g_part_table *table;
	struct sbuf *sb;
	quad_t end;
	unsigned int index;
	int error;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	pp = LIST_FIRST(&gp->consumer)->provider;
	table = gp->softc;
	end = gpp->gpp_start + gpp->gpp_size - 1;

	if (gpp->gpp_start < table->gpt_first ||
	    gpp->gpp_start > table->gpt_last) {
		gctl_error(req, "%d start '%jd'", EINVAL,
		    (intmax_t)gpp->gpp_start);
		return (EINVAL);
	}
	if (end < gpp->gpp_start || end > table->gpt_last) {
		gctl_error(req, "%d size '%jd'", EINVAL,
		    (intmax_t)gpp->gpp_size);
		return (EINVAL);
	}
	if (gpp->gpp_index > table->gpt_entries) {
		gctl_error(req, "%d index '%d'", EINVAL, gpp->gpp_index);
		return (EINVAL);
	}

	delent = last = NULL;
	index = (gpp->gpp_index > 0) ? gpp->gpp_index : 1;
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_deleted) {
			if (entry->gpe_index == index)
				delent = entry;
			continue;
		}
		if (entry->gpe_index == index)
			index = entry->gpe_index + 1;
		if (entry->gpe_index < index)
			last = entry;
		if (entry->gpe_internal)
			continue;
		if (gpp->gpp_start >= entry->gpe_start &&
		    gpp->gpp_start <= entry->gpe_end) {
			gctl_error(req, "%d start '%jd'", ENOSPC,
			    (intmax_t)gpp->gpp_start);
			return (ENOSPC);
		}
		if (end >= entry->gpe_start && end <= entry->gpe_end) {
			gctl_error(req, "%d end '%jd'", ENOSPC, (intmax_t)end);
			return (ENOSPC);
		}
		if (gpp->gpp_start < entry->gpe_start && end > entry->gpe_end) {
			gctl_error(req, "%d size '%jd'", ENOSPC,
			    (intmax_t)gpp->gpp_size);
			return (ENOSPC);
		}
	}
	if (gpp->gpp_index > 0 && index != gpp->gpp_index) {
		gctl_error(req, "%d index '%d'", EEXIST, gpp->gpp_index);
		return (EEXIST);
	}
	if (index > table->gpt_entries) {
		gctl_error(req, "%d index '%d'", ENOSPC, index);
		return (ENOSPC);
	}

	entry = (delent == NULL) ? g_malloc(table->gpt_scheme->gps_entrysz,
	    M_WAITOK | M_ZERO) : delent;
	entry->gpe_index = index;
	entry->gpe_start = gpp->gpp_start;
	entry->gpe_end = end;
	error = G_PART_ADD(table, entry, gpp);
	if (error) {
		gctl_error(req, "%d", error);
		if (delent == NULL)
			g_free(entry);
		return (error);
	}
	if (delent == NULL) {
		if (last == NULL)
			LIST_INSERT_HEAD(&table->gpt_entry, entry, gpe_entry);
		else
			LIST_INSERT_AFTER(last, entry, gpe_entry);
		entry->gpe_created = 1;
	} else {
		entry->gpe_deleted = 0;
		entry->gpe_modified = 1;
	}
	g_part_new_provider(gp, table, entry);

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		G_PART_FULLNAME(table, entry, sb, gp->name);
		if (pp->stripesize > 0 && entry->gpe_pp->stripeoffset != 0)
			sbuf_printf(sb, " added, but partition is not "
			    "aligned on %ju bytes\n", (uintmax_t)pp->stripesize);
		else
			sbuf_cat(sb, " added\n");
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_bootcode(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_geom *gp;
	struct g_part_table *table;
	struct sbuf *sb;
	int error, sz;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;
	sz = table->gpt_scheme->gps_bootcodesz;
	if (sz == 0) {
		error = ENODEV;
		goto fail;
	}
	if (gpp->gpp_codesize > sz) {
		error = EFBIG;
		goto fail;
	}

	error = G_PART_BOOTCODE(table, gpp);
	if (error)
		goto fail;

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		sbuf_printf(sb, "bootcode written to %s\n", gp->name);
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);

 fail:
	gctl_error(req, "%d", error);
	return (error);
}

static int
g_part_ctl_commit(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_entry *entry, *tmp;
	struct g_part_table *table;
	char *buf;
	int error, i;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;
	if (!table->gpt_opened) {
		gctl_error(req, "%d", EPERM);
		return (EPERM);
	}

	g_topology_unlock();

	cp = LIST_FIRST(&gp->consumer);
	if ((table->gpt_smhead | table->gpt_smtail) != 0) {
		pp = cp->provider;
		buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
		while (table->gpt_smhead != 0) {
			i = ffs(table->gpt_smhead) - 1;
			error = g_write_data(cp, i * pp->sectorsize, buf,
			    pp->sectorsize);
			if (error) {
				g_free(buf);
				goto fail;
			}
			table->gpt_smhead &= ~(1 << i);
		}
		while (table->gpt_smtail != 0) {
			i = ffs(table->gpt_smtail) - 1;
			error = g_write_data(cp, pp->mediasize - (i + 1) *
			    pp->sectorsize, buf, pp->sectorsize);
			if (error) {
				g_free(buf);
				goto fail;
			}
			table->gpt_smtail &= ~(1 << i);
		}
		g_free(buf);
	}

	if (table->gpt_scheme == &g_part_null_scheme) {
		g_topology_lock();
		g_access(cp, -1, -1, -1);
		g_part_wither(gp, ENXIO);
		return (0);
	}

	error = G_PART_WRITE(table, cp);
	if (error)
		goto fail;

	LIST_FOREACH_SAFE(entry, &table->gpt_entry, gpe_entry, tmp) {
		if (!entry->gpe_deleted) {
			/* Notify consumers that provider might be changed. */
			if (entry->gpe_modified && (
			    entry->gpe_pp->acw + entry->gpe_pp->ace +
			    entry->gpe_pp->acr) == 0)
				g_media_changed(entry->gpe_pp, M_NOWAIT);
			entry->gpe_created = 0;
			entry->gpe_modified = 0;
			continue;
		}
		LIST_REMOVE(entry, gpe_entry);
		g_free(entry);
	}
	table->gpt_created = 0;
	table->gpt_opened = 0;

	g_topology_lock();
	g_access(cp, -1, -1, -1);
	return (0);

fail:
	g_topology_lock();
	gctl_error(req, "%d", error);
	return (error);
}

static int
g_part_ctl_create(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_scheme *scheme;
	struct g_part_table *null, *table;
	struct sbuf *sb;
	int attr, error;

	pp = gpp->gpp_provider;
	scheme = gpp->gpp_scheme;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, pp->name));
	g_topology_assert();

	/* Check that there isn't already a g_part geom on the provider. */
	gp = g_part_find_geom(pp->name);
	if (gp != NULL) {
		null = gp->softc;
		if (null->gpt_scheme != &g_part_null_scheme) {
			gctl_error(req, "%d geom '%s'", EEXIST, pp->name);
			return (EEXIST);
		}
	} else
		null = NULL;

	if ((gpp->gpp_parms & G_PART_PARM_ENTRIES) &&
	    (gpp->gpp_entries < scheme->gps_minent ||
	     gpp->gpp_entries > scheme->gps_maxent)) {
		gctl_error(req, "%d entries '%d'", EINVAL, gpp->gpp_entries);
		return (EINVAL);
	}

	if (null == NULL)
		gp = g_new_geomf(&g_part_class, "%s", pp->name);
	gp->softc = kobj_create((kobj_class_t)gpp->gpp_scheme, M_GEOM,
	    M_WAITOK);
	table = gp->softc;
	table->gpt_gp = gp;
	table->gpt_scheme = gpp->gpp_scheme;
	table->gpt_entries = (gpp->gpp_parms & G_PART_PARM_ENTRIES) ?
	    gpp->gpp_entries : scheme->gps_minent;
	LIST_INIT(&table->gpt_entry);
	if (null == NULL) {
		cp = g_new_consumer(gp);
		cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
		error = g_attach(cp, pp);
		if (error == 0)
			error = g_access(cp, 1, 1, 1);
		if (error != 0) {
			g_part_wither(gp, error);
			gctl_error(req, "%d geom '%s'", error, pp->name);
			return (error);
		}
		table->gpt_opened = 1;
	} else {
		cp = LIST_FIRST(&gp->consumer);
		table->gpt_opened = null->gpt_opened;
		table->gpt_smhead = null->gpt_smhead;
		table->gpt_smtail = null->gpt_smtail;
	}

	g_topology_unlock();

	/* Make sure the provider has media. */
	if (pp->mediasize == 0 || pp->sectorsize == 0) {
		error = ENODEV;
		goto fail;
	}

	/* Make sure we can nest and if so, determine our depth. */
	error = g_getattr("PART::isleaf", cp, &attr);
	if (!error && attr) {
		error = ENODEV;
		goto fail;
	}
	error = g_getattr("PART::depth", cp, &attr);
	table->gpt_depth = (!error) ? attr + 1 : 0;

	/*
	 * Synthesize a disk geometry. Some partitioning schemes
	 * depend on it and since some file systems need it even
	 * when the partitition scheme doesn't, we do it here in
	 * scheme-independent code.
	 */
	g_part_geometry(table, cp, pp->mediasize / pp->sectorsize);

	error = G_PART_CREATE(table, gpp);
	if (error)
		goto fail;

	g_topology_lock();

	table->gpt_created = 1;
	if (null != NULL)
		kobj_delete((kobj_t)null, M_GEOM);

	/*
	 * Support automatic commit by filling in the gpp_geom
	 * parameter.
	 */
	gpp->gpp_parms |= G_PART_PARM_GEOM;
	gpp->gpp_geom = gp;

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		sbuf_printf(sb, "%s created\n", gp->name);
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);

fail:
	g_topology_lock();
	if (null == NULL) {
		g_access(cp, -1, -1, -1);
		g_part_wither(gp, error);
	} else {
		kobj_delete((kobj_t)gp->softc, M_GEOM);
		gp->softc = null;
	}
	gctl_error(req, "%d provider", error);
	return (error);
}

static int
g_part_ctl_delete(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct sbuf *sb;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;

	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_deleted || entry->gpe_internal)
			continue;
		if (entry->gpe_index == gpp->gpp_index)
			break;
	}
	if (entry == NULL) {
		gctl_error(req, "%d index '%d'", ENOENT, gpp->gpp_index);
		return (ENOENT);
	}

	pp = entry->gpe_pp;
	if (pp != NULL) {
		if (pp->acr > 0 || pp->acw > 0 || pp->ace > 0) {
			gctl_error(req, "%d", EBUSY);
			return (EBUSY);
		}

		pp->private = NULL;
		entry->gpe_pp = NULL;
	}

	if (pp != NULL)
		g_wither_provider(pp, ENXIO);

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		G_PART_FULLNAME(table, entry, sb, gp->name);
		sbuf_cat(sb, " deleted\n");
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}

	if (entry->gpe_created) {
		LIST_REMOVE(entry, gpe_entry);
		g_free(entry);
	} else {
		entry->gpe_modified = 0;
		entry->gpe_deleted = 1;
	}
	return (0);
}

static int
g_part_ctl_destroy(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_entry *entry, *tmp;
	struct g_part_table *null, *table;
	struct sbuf *sb;
	int error;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;
	/* Check for busy providers. */
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_deleted || entry->gpe_internal)
			continue;
		if (gpp->gpp_force) {
			pp = entry->gpe_pp;
			if (pp == NULL)
				continue;
			if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0)
				continue;
		}
		gctl_error(req, "%d", EBUSY);
		return (EBUSY);
	}

	if (gpp->gpp_force) {
		/* Destroy all providers. */
		LIST_FOREACH_SAFE(entry, &table->gpt_entry, gpe_entry, tmp) {
			pp = entry->gpe_pp;
			if (pp != NULL) {
				pp->private = NULL;
				g_wither_provider(pp, ENXIO);
			}
			LIST_REMOVE(entry, gpe_entry);
			g_free(entry);
		}
	}

	error = G_PART_DESTROY(table, gpp);
	if (error) {
		gctl_error(req, "%d", error);
		return (error);
	}

	gp->softc = kobj_create((kobj_class_t)&g_part_null_scheme, M_GEOM,
	    M_WAITOK);
	null = gp->softc;
	null->gpt_gp = gp;
	null->gpt_scheme = &g_part_null_scheme;
	LIST_INIT(&null->gpt_entry);

	cp = LIST_FIRST(&gp->consumer);
	pp = cp->provider;
	null->gpt_last = pp->mediasize / pp->sectorsize - 1;

	null->gpt_depth = table->gpt_depth;
	null->gpt_opened = table->gpt_opened;
	null->gpt_smhead = table->gpt_smhead;
	null->gpt_smtail = table->gpt_smtail;

	while ((entry = LIST_FIRST(&table->gpt_entry)) != NULL) {
		LIST_REMOVE(entry, gpe_entry);
		g_free(entry);
	}
	kobj_delete((kobj_t)table, M_GEOM);

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		sbuf_printf(sb, "%s destroyed\n", gp->name);
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_modify(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_geom *gp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct sbuf *sb;
	int error;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;

	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_deleted || entry->gpe_internal)
			continue;
		if (entry->gpe_index == gpp->gpp_index)
			break;
	}
	if (entry == NULL) {
		gctl_error(req, "%d index '%d'", ENOENT, gpp->gpp_index);
		return (ENOENT);
	}

	error = G_PART_MODIFY(table, entry, gpp);
	if (error) {
		gctl_error(req, "%d", error);
		return (error);
	}

	if (!entry->gpe_created)
		entry->gpe_modified = 1;

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		G_PART_FULLNAME(table, entry, sb, gp->name);
		sbuf_cat(sb, " modified\n");
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_move(struct gctl_req *req, struct g_part_parms *gpp)
{
	gctl_error(req, "%d verb 'move'", ENOSYS);
	return (ENOSYS);
}

static int
g_part_ctl_recover(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_part_table *table;
	struct g_geom *gp;
	struct sbuf *sb;
	int error, recovered;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();
	table = gp->softc;
	error = recovered = 0;

	if (table->gpt_corrupt) {
		error = G_PART_RECOVER(table);
		if (error == 0)
			error = g_part_check_integrity(table,
			    LIST_FIRST(&gp->consumer));
		if (error) {
			gctl_error(req, "%d recovering '%s' failed",
			    error, gp->name);
			return (error);
		}
		recovered = 1;
	}
	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		if (recovered)
			sbuf_printf(sb, "%s recovered\n", gp->name);
		else
			sbuf_printf(sb, "%s recovering is not needed\n",
			    gp->name);
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_resize(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_entry *pe, *entry;
	struct g_part_table *table;
	struct sbuf *sb;
	quad_t end;
	int error;
	off_t mediasize;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();
	table = gp->softc;

	/* check gpp_index */
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (entry->gpe_deleted || entry->gpe_internal)
			continue;
		if (entry->gpe_index == gpp->gpp_index)
			break;
	}
	if (entry == NULL) {
		gctl_error(req, "%d index '%d'", ENOENT, gpp->gpp_index);
		return (ENOENT);
	}

	/* check gpp_size */
	end = entry->gpe_start + gpp->gpp_size - 1;
	if (gpp->gpp_size < 1 || end > table->gpt_last) {
		gctl_error(req, "%d size '%jd'", EINVAL,
		    (intmax_t)gpp->gpp_size);
		return (EINVAL);
	}

	LIST_FOREACH(pe, &table->gpt_entry, gpe_entry) {
		if (pe->gpe_deleted || pe->gpe_internal || pe == entry)
			continue;
		if (end >= pe->gpe_start && end <= pe->gpe_end) {
			gctl_error(req, "%d end '%jd'", ENOSPC,
			    (intmax_t)end);
			return (ENOSPC);
		}
		if (entry->gpe_start < pe->gpe_start && end > pe->gpe_end) {
			gctl_error(req, "%d size '%jd'", ENOSPC,
			    (intmax_t)gpp->gpp_size);
			return (ENOSPC);
		}
	}

	pp = entry->gpe_pp;
	if ((g_debugflags & 16) == 0 &&
	    (pp->acr > 0 || pp->acw > 0 || pp->ace > 0)) {
		if (entry->gpe_end - entry->gpe_start + 1 > gpp->gpp_size) {
			/* Deny shrinking of an opened partition. */
			gctl_error(req, "%d", EBUSY);
			return (EBUSY);
		}
	}

	error = G_PART_RESIZE(table, entry, gpp);
	if (error) {
		gctl_error(req, "%d%s", error, error != EBUSY ? "":
		    " resizing will lead to unexpected shrinking"
		    " due to alignment");
		return (error);
	}

	if (!entry->gpe_created)
		entry->gpe_modified = 1;

	/* update mediasize of changed provider */
	mediasize = (entry->gpe_end - entry->gpe_start + 1) *
		pp->sectorsize;
	g_resize_provider(pp, mediasize);

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		G_PART_FULLNAME(table, entry, sb, gp->name);
		sbuf_cat(sb, " resized\n");
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_setunset(struct gctl_req *req, struct g_part_parms *gpp,
    unsigned int set)
{
	struct g_geom *gp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct sbuf *sb;
	int error;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;

	if (gpp->gpp_parms & G_PART_PARM_INDEX) {
		LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
			if (entry->gpe_deleted || entry->gpe_internal)
				continue;
			if (entry->gpe_index == gpp->gpp_index)
				break;
		}
		if (entry == NULL) {
			gctl_error(req, "%d index '%d'", ENOENT,
			    gpp->gpp_index);
			return (ENOENT);
		}
	} else
		entry = NULL;

	error = G_PART_SETUNSET(table, entry, gpp->gpp_attrib, set);
	if (error) {
		gctl_error(req, "%d attrib '%s'", error, gpp->gpp_attrib);
		return (error);
	}

	/* Provide feedback if so requested. */
	if (gpp->gpp_parms & G_PART_PARM_OUTPUT) {
		sb = sbuf_new_auto();
		sbuf_printf(sb, "%s %sset on ", gpp->gpp_attrib,
		    (set) ? "" : "un");
		if (entry)
			G_PART_FULLNAME(table, entry, sb, gp->name);
		else
			sbuf_cat(sb, gp->name);
		sbuf_cat(sb, "\n");
		sbuf_finish(sb);
		gctl_set_param(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);
	}
	return (0);
}

static int
g_part_ctl_undo(struct gctl_req *req, struct g_part_parms *gpp)
{
	struct g_consumer *cp;
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_part_entry *entry, *tmp;
	struct g_part_table *table;
	int error, reprobe;

	gp = gpp->gpp_geom;
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, gp->name));
	g_topology_assert();

	table = gp->softc;
	if (!table->gpt_opened) {
		gctl_error(req, "%d", EPERM);
		return (EPERM);
	}

	cp = LIST_FIRST(&gp->consumer);
	LIST_FOREACH_SAFE(entry, &table->gpt_entry, gpe_entry, tmp) {
		entry->gpe_modified = 0;
		if (entry->gpe_created) {
			pp = entry->gpe_pp;
			if (pp != NULL) {
				pp->private = NULL;
				entry->gpe_pp = NULL;
				g_wither_provider(pp, ENXIO);
			}
			entry->gpe_deleted = 1;
		}
		if (entry->gpe_deleted) {
			LIST_REMOVE(entry, gpe_entry);
			g_free(entry);
		}
	}

	g_topology_unlock();

	reprobe = (table->gpt_scheme == &g_part_null_scheme ||
	    table->gpt_created) ? 1 : 0;

	if (reprobe) {
		LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
			if (entry->gpe_internal)
				continue;
			error = EBUSY;
			goto fail;
		}
		while ((entry = LIST_FIRST(&table->gpt_entry)) != NULL) {
			LIST_REMOVE(entry, gpe_entry);
			g_free(entry);
		}
		error = g_part_probe(gp, cp, table->gpt_depth);
		if (error) {
			g_topology_lock();
			g_access(cp, -1, -1, -1);
			g_part_wither(gp, error);
			return (0);
		}
		table = gp->softc;

		/*
		 * Synthesize a disk geometry. Some partitioning schemes
		 * depend on it and since some file systems need it even
		 * when the partitition scheme doesn't, we do it here in
		 * scheme-independent code.
		 */
		pp = cp->provider;
		g_part_geometry(table, cp, pp->mediasize / pp->sectorsize);
	}

	error = G_PART_READ(table, cp);
	if (error)
		goto fail;
	error = g_part_check_integrity(table, cp);
	if (error)
		goto fail;

	g_topology_lock();
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (!entry->gpe_internal)
			g_part_new_provider(gp, table, entry);
	}

	table->gpt_opened = 0;
	g_access(cp, -1, -1, -1);
	return (0);

fail:
	g_topology_lock();
	gctl_error(req, "%d", error);
	return (error);
}

static void
g_part_wither(struct g_geom *gp, int error)
{
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct g_provider *pp;

	table = gp->softc;
	if (table != NULL) {
		gp->softc = NULL;
		while ((entry = LIST_FIRST(&table->gpt_entry)) != NULL) {
			LIST_REMOVE(entry, gpe_entry);
			pp = entry->gpe_pp;
			entry->gpe_pp = NULL;
			if (pp != NULL) {
				pp->private = NULL;
				g_wither_provider(pp, error);
			}
			g_free(entry);
		}
		G_PART_DESTROY(table, NULL);
		kobj_delete((kobj_t)table, M_GEOM);
	}
	g_wither_geom(gp, error);
}

/*
 * Class methods.
 */

static void
g_part_ctlreq(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	struct g_part_parms gpp;
	struct g_part_table *table;
	struct gctl_req_arg *ap;
	enum g_part_ctl ctlreq;
	unsigned int i, mparms, oparms, parm;
	int auto_commit, close_on_error;
	int error, modifies;

	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s,%s)", __func__, mp->name, verb));
	g_topology_assert();

	ctlreq = G_PART_CTL_NONE;
	modifies = 1;
	mparms = 0;
	oparms = G_PART_PARM_FLAGS | G_PART_PARM_OUTPUT | G_PART_PARM_VERSION;
	switch (*verb) {
	case 'a':
		if (!strcmp(verb, "add")) {
			ctlreq = G_PART_CTL_ADD;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_SIZE |
			    G_PART_PARM_START | G_PART_PARM_TYPE;
			oparms |= G_PART_PARM_INDEX | G_PART_PARM_LABEL;
		}
		break;
	case 'b':
		if (!strcmp(verb, "bootcode")) {
			ctlreq = G_PART_CTL_BOOTCODE;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_BOOTCODE;
			oparms |= G_PART_PARM_SKIP_DSN;
		}
		break;
	case 'c':
		if (!strcmp(verb, "commit")) {
			ctlreq = G_PART_CTL_COMMIT;
			mparms |= G_PART_PARM_GEOM;
			modifies = 0;
		} else if (!strcmp(verb, "create")) {
			ctlreq = G_PART_CTL_CREATE;
			mparms |= G_PART_PARM_PROVIDER | G_PART_PARM_SCHEME;
			oparms |= G_PART_PARM_ENTRIES;
		}
		break;
	case 'd':
		if (!strcmp(verb, "delete")) {
			ctlreq = G_PART_CTL_DELETE;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_INDEX;
		} else if (!strcmp(verb, "destroy")) {
			ctlreq = G_PART_CTL_DESTROY;
			mparms |= G_PART_PARM_GEOM;
			oparms |= G_PART_PARM_FORCE;
		}
		break;
	case 'm':
		if (!strcmp(verb, "modify")) {
			ctlreq = G_PART_CTL_MODIFY;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_INDEX;
			oparms |= G_PART_PARM_LABEL | G_PART_PARM_TYPE;
		} else if (!strcmp(verb, "move")) {
			ctlreq = G_PART_CTL_MOVE;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_INDEX;
		}
		break;
	case 'r':
		if (!strcmp(verb, "recover")) {
			ctlreq = G_PART_CTL_RECOVER;
			mparms |= G_PART_PARM_GEOM;
		} else if (!strcmp(verb, "resize")) {
			ctlreq = G_PART_CTL_RESIZE;
			mparms |= G_PART_PARM_GEOM | G_PART_PARM_INDEX |
			    G_PART_PARM_SIZE;
		}
		break;
	case 's':
		if (!strcmp(verb, "set")) {
			ctlreq = G_PART_CTL_SET;
			mparms |= G_PART_PARM_ATTRIB | G_PART_PARM_GEOM;
			oparms |= G_PART_PARM_INDEX;
		}
		break;
	case 'u':
		if (!strcmp(verb, "undo")) {
			ctlreq = G_PART_CTL_UNDO;
			mparms |= G_PART_PARM_GEOM;
			modifies = 0;
		} else if (!strcmp(verb, "unset")) {
			ctlreq = G_PART_CTL_UNSET;
			mparms |= G_PART_PARM_ATTRIB | G_PART_PARM_GEOM;
			oparms |= G_PART_PARM_INDEX;
		}
		break;
	}
	if (ctlreq == G_PART_CTL_NONE) {
		gctl_error(req, "%d verb '%s'", EINVAL, verb);
		return;
	}

	bzero(&gpp, sizeof(gpp));
	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		parm = 0;
		switch (ap->name[0]) {
		case 'a':
			if (!strcmp(ap->name, "arg0")) {
				parm = mparms &
				    (G_PART_PARM_GEOM | G_PART_PARM_PROVIDER);
			}
			if (!strcmp(ap->name, "attrib"))
				parm = G_PART_PARM_ATTRIB;
			break;
		case 'b':
			if (!strcmp(ap->name, "bootcode"))
				parm = G_PART_PARM_BOOTCODE;
			break;
		case 'c':
			if (!strcmp(ap->name, "class"))
				continue;
			break;
		case 'e':
			if (!strcmp(ap->name, "entries"))
				parm = G_PART_PARM_ENTRIES;
			break;
		case 'f':
			if (!strcmp(ap->name, "flags"))
				parm = G_PART_PARM_FLAGS;
			else if (!strcmp(ap->name, "force"))
				parm = G_PART_PARM_FORCE;
			break;
		case 'i':
			if (!strcmp(ap->name, "index"))
				parm = G_PART_PARM_INDEX;
			break;
		case 'l':
			if (!strcmp(ap->name, "label"))
				parm = G_PART_PARM_LABEL;
			break;
		case 'o':
			if (!strcmp(ap->name, "output"))
				parm = G_PART_PARM_OUTPUT;
			break;
		case 's':
			if (!strcmp(ap->name, "scheme"))
				parm = G_PART_PARM_SCHEME;
			else if (!strcmp(ap->name, "size"))
				parm = G_PART_PARM_SIZE;
			else if (!strcmp(ap->name, "start"))
				parm = G_PART_PARM_START;
			else if (!strcmp(ap->name, "skip_dsn"))
				parm = G_PART_PARM_SKIP_DSN;
			break;
		case 't':
			if (!strcmp(ap->name, "type"))
				parm = G_PART_PARM_TYPE;
			break;
		case 'v':
			if (!strcmp(ap->name, "verb"))
				continue;
			else if (!strcmp(ap->name, "version"))
				parm = G_PART_PARM_VERSION;
			break;
		}
		if ((parm & (mparms | oparms)) == 0) {
			gctl_error(req, "%d param '%s'", EINVAL, ap->name);
			return;
		}
		switch (parm) {
		case G_PART_PARM_ATTRIB:
			error = g_part_parm_str(req, ap->name,
			    &gpp.gpp_attrib);
			break;
		case G_PART_PARM_BOOTCODE:
			error = g_part_parm_bootcode(req, ap->name,
			    &gpp.gpp_codeptr, &gpp.gpp_codesize);
			break;
		case G_PART_PARM_ENTRIES:
			error = g_part_parm_intmax(req, ap->name,
			    &gpp.gpp_entries);
			break;
		case G_PART_PARM_FLAGS:
			error = g_part_parm_str(req, ap->name, &gpp.gpp_flags);
			break;
		case G_PART_PARM_FORCE:
			error = g_part_parm_uint32(req, ap->name,
			    &gpp.gpp_force);
			break;
		case G_PART_PARM_GEOM:
			error = g_part_parm_geom(req, ap->name, &gpp.gpp_geom);
			break;
		case G_PART_PARM_INDEX:
			error = g_part_parm_intmax(req, ap->name,
			    &gpp.gpp_index);
			break;
		case G_PART_PARM_LABEL:
			error = g_part_parm_str(req, ap->name, &gpp.gpp_label);
			break;
		case G_PART_PARM_OUTPUT:
			error = 0;	/* Write-only parameter */
			break;
		case G_PART_PARM_PROVIDER:
			error = g_part_parm_provider(req, ap->name,
			    &gpp.gpp_provider);
			break;
		case G_PART_PARM_SCHEME:
			error = g_part_parm_scheme(req, ap->name,
			    &gpp.gpp_scheme);
			break;
		case G_PART_PARM_SIZE:
			error = g_part_parm_quad(req, ap->name, &gpp.gpp_size);
			break;
		case G_PART_PARM_SKIP_DSN:
			error = g_part_parm_uint32(req, ap->name,
			    &gpp.gpp_skip_dsn);
			break;
		case G_PART_PARM_START:
			error = g_part_parm_quad(req, ap->name,
			    &gpp.gpp_start);
			break;
		case G_PART_PARM_TYPE:
			error = g_part_parm_str(req, ap->name, &gpp.gpp_type);
			break;
		case G_PART_PARM_VERSION:
			error = g_part_parm_uint32(req, ap->name,
			    &gpp.gpp_version);
			break;
		default:
			error = EDOOFUS;
			gctl_error(req, "%d %s", error, ap->name);
			break;
		}
		if (error != 0) {
			if (error == ENOATTR) {
				gctl_error(req, "%d param '%s'", error,
				    ap->name);
			}
			return;
		}
		gpp.gpp_parms |= parm;
	}
	if ((gpp.gpp_parms & mparms) != mparms) {
		parm = mparms - (gpp.gpp_parms & mparms);
		gctl_error(req, "%d param '%x'", ENOATTR, parm);
		return;
	}

	/* Obtain permissions if possible/necessary. */
	close_on_error = 0;
	table = NULL;
	if (modifies && (gpp.gpp_parms & G_PART_PARM_GEOM)) {
		table = gpp.gpp_geom->softc;
		if (table != NULL && table->gpt_corrupt &&
		    ctlreq != G_PART_CTL_DESTROY &&
		    ctlreq != G_PART_CTL_RECOVER) {
			gctl_error(req, "%d table '%s' is corrupt",
			    EPERM, gpp.gpp_geom->name);
			return;
		}
		if (table != NULL && !table->gpt_opened) {
			error = g_access(LIST_FIRST(&gpp.gpp_geom->consumer),
			    1, 1, 1);
			if (error) {
				gctl_error(req, "%d geom '%s'", error,
				    gpp.gpp_geom->name);
				return;
			}
			table->gpt_opened = 1;
			close_on_error = 1;
		}
	}

	/* Allow the scheme to check or modify the parameters. */
	if (table != NULL) {
		error = G_PART_PRECHECK(table, ctlreq, &gpp);
		if (error) {
			gctl_error(req, "%d pre-check failed", error);
			goto out;
		}
	} else
		error = EDOOFUS;	/* Prevent bogus uninit. warning. */

	switch (ctlreq) {
	case G_PART_CTL_NONE:
		panic("%s", __func__);
	case G_PART_CTL_ADD:
		error = g_part_ctl_add(req, &gpp);
		break;
	case G_PART_CTL_BOOTCODE:
		error = g_part_ctl_bootcode(req, &gpp);
		break;
	case G_PART_CTL_COMMIT:
		error = g_part_ctl_commit(req, &gpp);
		break;
	case G_PART_CTL_CREATE:
		error = g_part_ctl_create(req, &gpp);
		break;
	case G_PART_CTL_DELETE:
		error = g_part_ctl_delete(req, &gpp);
		break;
	case G_PART_CTL_DESTROY:
		error = g_part_ctl_destroy(req, &gpp);
		break;
	case G_PART_CTL_MODIFY:
		error = g_part_ctl_modify(req, &gpp);
		break;
	case G_PART_CTL_MOVE:
		error = g_part_ctl_move(req, &gpp);
		break;
	case G_PART_CTL_RECOVER:
		error = g_part_ctl_recover(req, &gpp);
		break;
	case G_PART_CTL_RESIZE:
		error = g_part_ctl_resize(req, &gpp);
		break;
	case G_PART_CTL_SET:
		error = g_part_ctl_setunset(req, &gpp, 1);
		break;
	case G_PART_CTL_UNDO:
		error = g_part_ctl_undo(req, &gpp);
		break;
	case G_PART_CTL_UNSET:
		error = g_part_ctl_setunset(req, &gpp, 0);
		break;
	}

	/* Implement automatic commit. */
	if (!error) {
		auto_commit = (modifies &&
		    (gpp.gpp_parms & G_PART_PARM_FLAGS) &&
		    strchr(gpp.gpp_flags, 'C') != NULL) ? 1 : 0;
		if (auto_commit) {
			KASSERT(gpp.gpp_parms & G_PART_PARM_GEOM, ("%s",
			    __func__));
			error = g_part_ctl_commit(req, &gpp);
		}
	}

 out:
	if (error && close_on_error) {
		g_access(LIST_FIRST(&gpp.gpp_geom->consumer), -1, -1, -1);
		table->gpt_opened = 0;
	}
}

static int
g_part_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp)
{

	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s,%s)", __func__, mp->name, gp->name));
	g_topology_assert();

	g_part_wither(gp, EINVAL);
	return (0);
}

static struct g_geom *
g_part_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct root_hold_token *rht;
	struct g_geom_alias *gap;
	int attr, depth;
	int error;

	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s,%s)", __func__, mp->name, pp->name));
	g_topology_assert();

	/* Skip providers that are already open for writing. */
	if (pp->acw > 0)
		return (NULL);

	/*
	 * Create a GEOM with consumer and hook it up to the provider.
	 * With that we become part of the topology. Obtain read access
	 * to the provider.
	 */
	gp = g_new_geomf(mp, "%s", pp->name);
	LIST_FOREACH(gap, &pp->geom->aliases, ga_next)
		g_geom_add_alias(gp, gap->ga_alias);
	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	error = g_attach(cp, pp);
	if (error == 0)
		error = g_access(cp, 1, 0, 0);
	if (error != 0) {
		if (cp->provider)
			g_detach(cp);
		g_destroy_consumer(cp);
		g_destroy_geom(gp);
		return (NULL);
	}

	rht = root_mount_hold(mp->name);
	g_topology_unlock();

	/*
	 * Short-circuit the whole probing galore when there's no
	 * media present.
	 */
	if (pp->mediasize == 0 || pp->sectorsize == 0) {
		error = ENODEV;
		goto fail;
	}

	/* Make sure we can nest and if so, determine our depth. */
	error = g_getattr("PART::isleaf", cp, &attr);
	if (!error && attr) {
		error = ENODEV;
		goto fail;
	}
	error = g_getattr("PART::depth", cp, &attr);
	depth = (!error) ? attr + 1 : 0;

	error = g_part_probe(gp, cp, depth);
	if (error)
		goto fail;

	table = gp->softc;

	/*
	 * Synthesize a disk geometry. Some partitioning schemes
	 * depend on it and since some file systems need it even
	 * when the partitition scheme doesn't, we do it here in
	 * scheme-independent code.
	 */
	g_part_geometry(table, cp, pp->mediasize / pp->sectorsize);

	error = G_PART_READ(table, cp);
	if (error)
		goto fail;
	error = g_part_check_integrity(table, cp);
	if (error)
		goto fail;

	g_topology_lock();
	LIST_FOREACH(entry, &table->gpt_entry, gpe_entry) {
		if (!entry->gpe_internal)
			g_part_new_provider(gp, table, entry);
	}

	root_mount_rel(rht);
	g_access(cp, -1, 0, 0);
	return (gp);

 fail:
	g_topology_lock();
	root_mount_rel(rht);
	g_access(cp, -1, 0, 0);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	return (NULL);
}

/*
 * Geom methods.
 */

static int
g_part_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_consumer *cp;

	G_PART_TRACE((G_T_ACCESS, "%s(%s,%d,%d,%d)", __func__, pp->name, dr,
	    dw, de));

	cp = LIST_FIRST(&pp->geom->consumer);

	/* We always gain write-exclusive access. */
	return (g_access(cp, dr, dw, dw + de));
}

static void
g_part_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	char buf[64];
	struct g_part_entry *entry;
	struct g_part_table *table;

	KASSERT(sb != NULL && gp != NULL, ("%s", __func__));
	table = gp->softc;

	if (indent == NULL) {
		KASSERT(cp == NULL && pp != NULL, ("%s", __func__));
		entry = pp->private;
		if (entry == NULL)
			return;
		sbuf_printf(sb, " i %u o %ju ty %s", entry->gpe_index,
		    (uintmax_t)entry->gpe_offset,
		    G_PART_TYPE(table, entry, buf, sizeof(buf)));
		/*
		 * libdisk compatibility quirk - the scheme dumps the
		 * slicer name and partition type in a way that is
		 * compatible with libdisk. When libdisk is not used
		 * anymore, this should go away.
		 */
		G_PART_DUMPCONF(table, entry, sb, indent);
	} else if (cp != NULL) {	/* Consumer configuration. */
		KASSERT(pp == NULL, ("%s", __func__));
		/* none */
	} else if (pp != NULL) {	/* Provider configuration. */
		entry = pp->private;
		if (entry == NULL)
			return;
		sbuf_printf(sb, "%s<start>%ju</start>\n", indent,
		    (uintmax_t)entry->gpe_start);
		sbuf_printf(sb, "%s<end>%ju</end>\n", indent,
		    (uintmax_t)entry->gpe_end);
		sbuf_printf(sb, "%s<index>%u</index>\n", indent,
		    entry->gpe_index);
		sbuf_printf(sb, "%s<type>%s</type>\n", indent,
		    G_PART_TYPE(table, entry, buf, sizeof(buf)));
		sbuf_printf(sb, "%s<offset>%ju</offset>\n", indent,
		    (uintmax_t)entry->gpe_offset);
		sbuf_printf(sb, "%s<length>%ju</length>\n", indent,
		    (uintmax_t)pp->mediasize);
		G_PART_DUMPCONF(table, entry, sb, indent);
	} else {			/* Geom configuration. */
		sbuf_printf(sb, "%s<scheme>%s</scheme>\n", indent,
		    table->gpt_scheme->name);
		sbuf_printf(sb, "%s<entries>%u</entries>\n", indent,
		    table->gpt_entries);
		sbuf_printf(sb, "%s<first>%ju</first>\n", indent,
		    (uintmax_t)table->gpt_first);
		sbuf_printf(sb, "%s<last>%ju</last>\n", indent,
		    (uintmax_t)table->gpt_last);
		sbuf_printf(sb, "%s<fwsectors>%u</fwsectors>\n", indent,
		    table->gpt_sectors);
		sbuf_printf(sb, "%s<fwheads>%u</fwheads>\n", indent,
		    table->gpt_heads);
		sbuf_printf(sb, "%s<state>%s</state>\n", indent,
		    table->gpt_corrupt ? "CORRUPT": "OK");
		sbuf_printf(sb, "%s<modified>%s</modified>\n", indent,
		    table->gpt_opened ? "true": "false");
		G_PART_DUMPCONF(table, NULL, sb, indent);
	}
}

/*-
 * This start routine is only called for non-trivial requests, all the
 * trivial ones are handled autonomously by the slice code.
 * For requests we handle here, we must call the g_io_deliver() on the
 * bio, and return non-zero to indicate to the slice code that we did so.
 * This code executes in the "DOWN" I/O path, this means:
 *    * No sleeping.
 *    * Don't grab the topology lock.
 *    * Don't call biowait, g_getattr(), g_setattr() or g_read_data()
 */
static int
g_part_ioctl(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td)
{
	struct g_part_table *table;

	table = pp->geom->softc;
	return G_PART_IOCTL(table, pp, cmd, data, fflag, td);
}

static void
g_part_resize(struct g_consumer *cp)
{
	struct g_part_table *table;

	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, cp->provider->name));
	g_topology_assert();

	if (auto_resize == 0)
		return;

	table = cp->geom->softc;
	if (table->gpt_opened == 0) {
		if (g_access(cp, 1, 1, 1) != 0)
			return;
		table->gpt_opened = 1;
	}
	if (G_PART_RESIZE(table, NULL, NULL) == 0)
		printf("GEOM_PART: %s was automatically resized.\n"
		    "  Use `gpart commit %s` to save changes or "
		    "`gpart undo %s` to revert them.\n", cp->geom->name,
		    cp->geom->name, cp->geom->name);
	if (g_part_check_integrity(table, cp) != 0) {
		g_access(cp, -1, -1, -1);
		table->gpt_opened = 0;
		g_part_wither(table->gpt_gp, ENXIO);
	}
}

static void
g_part_orphan(struct g_consumer *cp)
{
	struct g_provider *pp;
	struct g_part_table *table;

	pp = cp->provider;
	KASSERT(pp != NULL, ("%s", __func__));
	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, pp->name));
	g_topology_assert();

	KASSERT(pp->error != 0, ("%s", __func__));
	table = cp->geom->softc;
	if (table != NULL && table->gpt_opened)
		g_access(cp, -1, -1, -1);
	g_part_wither(cp->geom, pp->error);
}

static void
g_part_spoiled(struct g_consumer *cp)
{

	G_PART_TRACE((G_T_TOPOLOGY, "%s(%s)", __func__, cp->provider->name));
	g_topology_assert();

	cp->flags |= G_CF_ORPHAN;
	g_part_wither(cp->geom, ENXIO);
}

static void
g_part_start(struct bio *bp)
{
	struct bio *bp2;
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_part_entry *entry;
	struct g_part_table *table;
	struct g_kerneldump *gkd;
	struct g_provider *pp;
	void (*done_func)(struct bio *) = g_std_done;
	char buf[64];

	biotrack(bp, __func__);

	pp = bp->bio_to;
	gp = pp->geom;
	table = gp->softc;
	cp = LIST_FIRST(&gp->consumer);

	G_PART_TRACE((G_T_BIO, "%s: cmd=%d, provider=%s", __func__, bp->bio_cmd,
	    pp->name));

	entry = pp->private;
	if (entry == NULL) {
		g_io_deliver(bp, ENXIO);
		return;
	}

	switch(bp->bio_cmd) {
	case BIO_DELETE:
	case BIO_READ:
	case BIO_WRITE:
		if (bp->bio_offset >= pp->mediasize) {
			g_io_deliver(bp, EIO);
			return;
		}
		bp2 = g_clone_bio(bp);
		if (bp2 == NULL) {
			g_io_deliver(bp, ENOMEM);
			return;
		}
		if (bp2->bio_offset + bp2->bio_length > pp->mediasize)
			bp2->bio_length = pp->mediasize - bp2->bio_offset;
		bp2->bio_done = g_std_done;
		bp2->bio_offset += entry->gpe_offset;
		g_io_request(bp2, cp);
		return;
	case BIO_FLUSH:
		break;
	case BIO_GETATTR:
		if (g_handleattr_int(bp, "GEOM::fwheads", table->gpt_heads))
			return;
		if (g_handleattr_int(bp, "GEOM::fwsectors", table->gpt_sectors))
			return;
		if (g_handleattr_int(bp, "PART::isleaf", table->gpt_isleaf))
			return;
		if (g_handleattr_int(bp, "PART::depth", table->gpt_depth))
			return;
		if (g_handleattr_str(bp, "PART::scheme",
		    table->gpt_scheme->name))
			return;
		if (g_handleattr_str(bp, "PART::type",
		    G_PART_TYPE(table, entry, buf, sizeof(buf))))
			return;
		if (!strcmp("GEOM::physpath", bp->bio_attribute)) {
			done_func = g_part_get_physpath_done;
			break;
		}
		if (!strcmp("GEOM::kerneldump", bp->bio_attribute)) {
			/*
			 * Check that the partition is suitable for kernel
			 * dumps. Typically only swap partitions should be
			 * used. If the request comes from the nested scheme
			 * we allow dumping there as well.
			 */
			if ((bp->bio_from == NULL ||
			    bp->bio_from->geom->class != &g_part_class) &&
			    G_PART_DUMPTO(table, entry) == 0) {
				g_io_deliver(bp, ENODEV);
				printf("GEOM_PART: Partition '%s' not suitable"
				    " for kernel dumps (wrong type?)\n",
				    pp->name);
				return;
			}
			gkd = (struct g_kerneldump *)bp->bio_data;
			if (gkd->offset >= pp->mediasize) {
				g_io_deliver(bp, EIO);
				return;
			}
			if (gkd->offset + gkd->length > pp->mediasize)
				gkd->length = pp->mediasize - gkd->offset;
			gkd->offset += entry->gpe_offset;
		}
		break;
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	bp2 = g_clone_bio(bp);
	if (bp2 == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}
	bp2->bio_done = done_func;
	g_io_request(bp2, cp);
}

static void
g_part_init(struct g_class *mp)
{

	TAILQ_INSERT_HEAD(&g_part_schemes, &g_part_null_scheme, scheme_list);
}

static void
g_part_fini(struct g_class *mp)
{

	TAILQ_REMOVE(&g_part_schemes, &g_part_null_scheme, scheme_list);
}

static void
g_part_unload_event(void *arg, int flag)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_part_scheme *scheme;
	struct g_part_table *table;
	uintptr_t *xchg;
	int acc, error;

	if (flag == EV_CANCEL)
		return;

	xchg = arg;
	error = 0;
	scheme = (void *)(*xchg);

	g_topology_assert();

	LIST_FOREACH(gp, &g_part_class.geom, geom) {
		table = gp->softc;
		if (table->gpt_scheme != scheme)
			continue;

		acc = 0;
		LIST_FOREACH(pp, &gp->provider, provider)
			acc += pp->acr + pp->acw + pp->ace;
		LIST_FOREACH(cp, &gp->consumer, consumer)
			acc += cp->acr + cp->acw + cp->ace;

		if (!acc)
			g_part_wither(gp, ENOSYS);
		else
			error = EBUSY;
	}

	if (!error)
		TAILQ_REMOVE(&g_part_schemes, scheme, scheme_list);

	*xchg = error;
}

int
g_part_modevent(module_t mod, int type, struct g_part_scheme *scheme)
{
	struct g_part_scheme *iter;
	uintptr_t arg;
	int error;

	error = 0;
	switch (type) {
	case MOD_LOAD:
		TAILQ_FOREACH(iter, &g_part_schemes, scheme_list) {
			if (scheme == iter) {
				printf("GEOM_PART: scheme %s is already "
				    "registered!\n", scheme->name);
				break;
			}
		}
		if (iter == NULL) {
			TAILQ_INSERT_TAIL(&g_part_schemes, scheme,
			    scheme_list);
			g_retaste(&g_part_class);
		}
		break;
	case MOD_UNLOAD:
		arg = (uintptr_t)scheme;
		error = g_waitfor_event(g_part_unload_event, &arg, M_WAITOK,
		    NULL);
		if (error == 0)
			error = arg;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}
