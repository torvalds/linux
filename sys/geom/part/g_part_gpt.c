/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2005-2007, 2011 Marcel Moolenaar
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
#include <sys/diskmbr.h>
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
#include <sys/uuid.h>
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_gpt, "GEOM partitioning class for GPT partitions support");

CTASSERT(offsetof(struct gpt_hdr, padding) == 92);
CTASSERT(sizeof(struct gpt_ent) == 128);

#define	EQUUID(a,b)	(memcmp(a, b, sizeof(struct uuid)) == 0)

#define	MBRSIZE		512

enum gpt_elt {
	GPT_ELT_PRIHDR,
	GPT_ELT_PRITBL,
	GPT_ELT_SECHDR,
	GPT_ELT_SECTBL,
	GPT_ELT_COUNT
};

enum gpt_state {
	GPT_STATE_UNKNOWN,	/* Not determined. */
	GPT_STATE_MISSING,	/* No signature found. */
	GPT_STATE_CORRUPT,	/* Checksum mismatch. */
	GPT_STATE_INVALID,	/* Nonconformant/invalid. */
	GPT_STATE_OK		/* Perfectly fine. */
};

struct g_part_gpt_table {
	struct g_part_table	base;
	u_char			mbr[MBRSIZE];
	struct gpt_hdr		*hdr;
	quad_t			lba[GPT_ELT_COUNT];
	enum gpt_state		state[GPT_ELT_COUNT];
	int			bootcamp;
};

struct g_part_gpt_entry {
	struct g_part_entry	base;
	struct gpt_ent		ent;
};

static void g_gpt_printf_utf16(struct sbuf *, uint16_t *, size_t);
static void g_gpt_utf8_to_utf16(const uint8_t *, uint16_t *, size_t);
static void g_gpt_set_defaults(struct g_part_table *, struct g_provider *);

static int g_part_gpt_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_gpt_bootcode(struct g_part_table *, struct g_part_parms *);
static int g_part_gpt_create(struct g_part_table *, struct g_part_parms *);
static int g_part_gpt_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_gpt_dumpconf(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
static int g_part_gpt_dumpto(struct g_part_table *, struct g_part_entry *);
static int g_part_gpt_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_gpt_name(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_gpt_probe(struct g_part_table *, struct g_consumer *);
static int g_part_gpt_read(struct g_part_table *, struct g_consumer *);
static int g_part_gpt_setunset(struct g_part_table *table,
    struct g_part_entry *baseentry, const char *attrib, unsigned int set);
static const char *g_part_gpt_type(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_gpt_write(struct g_part_table *, struct g_consumer *);
static int g_part_gpt_resize(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_gpt_recover(struct g_part_table *);

static kobj_method_t g_part_gpt_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_gpt_add),
	KOBJMETHOD(g_part_bootcode,	g_part_gpt_bootcode),
	KOBJMETHOD(g_part_create,	g_part_gpt_create),
	KOBJMETHOD(g_part_destroy,	g_part_gpt_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_gpt_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_gpt_dumpto),
	KOBJMETHOD(g_part_modify,	g_part_gpt_modify),
	KOBJMETHOD(g_part_resize,	g_part_gpt_resize),
	KOBJMETHOD(g_part_name,		g_part_gpt_name),
	KOBJMETHOD(g_part_probe,	g_part_gpt_probe),
	KOBJMETHOD(g_part_read,		g_part_gpt_read),
	KOBJMETHOD(g_part_recover,	g_part_gpt_recover),
	KOBJMETHOD(g_part_setunset,	g_part_gpt_setunset),
	KOBJMETHOD(g_part_type,		g_part_gpt_type),
	KOBJMETHOD(g_part_write,	g_part_gpt_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_gpt_scheme = {
	"GPT",
	g_part_gpt_methods,
	sizeof(struct g_part_gpt_table),
	.gps_entrysz = sizeof(struct g_part_gpt_entry),
	.gps_minent = 128,
	.gps_maxent = 4096,
	.gps_bootcodesz = MBRSIZE,
};
G_PART_SCHEME_DECLARE(g_part_gpt);
MODULE_VERSION(geom_part_gpt, 0);

static struct uuid gpt_uuid_apple_apfs = GPT_ENT_TYPE_APPLE_APFS;
static struct uuid gpt_uuid_apple_boot = GPT_ENT_TYPE_APPLE_BOOT;
static struct uuid gpt_uuid_apple_core_storage =
    GPT_ENT_TYPE_APPLE_CORE_STORAGE;
static struct uuid gpt_uuid_apple_hfs = GPT_ENT_TYPE_APPLE_HFS;
static struct uuid gpt_uuid_apple_label = GPT_ENT_TYPE_APPLE_LABEL;
static struct uuid gpt_uuid_apple_raid = GPT_ENT_TYPE_APPLE_RAID;
static struct uuid gpt_uuid_apple_raid_offline = GPT_ENT_TYPE_APPLE_RAID_OFFLINE;
static struct uuid gpt_uuid_apple_tv_recovery = GPT_ENT_TYPE_APPLE_TV_RECOVERY;
static struct uuid gpt_uuid_apple_ufs = GPT_ENT_TYPE_APPLE_UFS;
static struct uuid gpt_uuid_bios_boot = GPT_ENT_TYPE_BIOS_BOOT;
static struct uuid gpt_uuid_chromeos_firmware = GPT_ENT_TYPE_CHROMEOS_FIRMWARE;
static struct uuid gpt_uuid_chromeos_kernel = GPT_ENT_TYPE_CHROMEOS_KERNEL;
static struct uuid gpt_uuid_chromeos_reserved = GPT_ENT_TYPE_CHROMEOS_RESERVED;
static struct uuid gpt_uuid_chromeos_root = GPT_ENT_TYPE_CHROMEOS_ROOT;
static struct uuid gpt_uuid_dfbsd_ccd = GPT_ENT_TYPE_DRAGONFLY_CCD;
static struct uuid gpt_uuid_dfbsd_hammer = GPT_ENT_TYPE_DRAGONFLY_HAMMER;
static struct uuid gpt_uuid_dfbsd_hammer2 = GPT_ENT_TYPE_DRAGONFLY_HAMMER2;
static struct uuid gpt_uuid_dfbsd_label32 = GPT_ENT_TYPE_DRAGONFLY_LABEL32;
static struct uuid gpt_uuid_dfbsd_label64 = GPT_ENT_TYPE_DRAGONFLY_LABEL64;
static struct uuid gpt_uuid_dfbsd_legacy = GPT_ENT_TYPE_DRAGONFLY_LEGACY;
static struct uuid gpt_uuid_dfbsd_swap = GPT_ENT_TYPE_DRAGONFLY_SWAP;
static struct uuid gpt_uuid_dfbsd_ufs1 = GPT_ENT_TYPE_DRAGONFLY_UFS1;
static struct uuid gpt_uuid_dfbsd_vinum = GPT_ENT_TYPE_DRAGONFLY_VINUM;
static struct uuid gpt_uuid_efi = GPT_ENT_TYPE_EFI;
static struct uuid gpt_uuid_freebsd = GPT_ENT_TYPE_FREEBSD;
static struct uuid gpt_uuid_freebsd_boot = GPT_ENT_TYPE_FREEBSD_BOOT;
static struct uuid gpt_uuid_freebsd_nandfs = GPT_ENT_TYPE_FREEBSD_NANDFS;
static struct uuid gpt_uuid_freebsd_swap = GPT_ENT_TYPE_FREEBSD_SWAP;
static struct uuid gpt_uuid_freebsd_ufs = GPT_ENT_TYPE_FREEBSD_UFS;
static struct uuid gpt_uuid_freebsd_vinum = GPT_ENT_TYPE_FREEBSD_VINUM;
static struct uuid gpt_uuid_freebsd_zfs = GPT_ENT_TYPE_FREEBSD_ZFS;
static struct uuid gpt_uuid_linux_data = GPT_ENT_TYPE_LINUX_DATA;
static struct uuid gpt_uuid_linux_lvm = GPT_ENT_TYPE_LINUX_LVM;
static struct uuid gpt_uuid_linux_raid = GPT_ENT_TYPE_LINUX_RAID;
static struct uuid gpt_uuid_linux_swap = GPT_ENT_TYPE_LINUX_SWAP;
static struct uuid gpt_uuid_mbr = GPT_ENT_TYPE_MBR;
static struct uuid gpt_uuid_ms_basic_data = GPT_ENT_TYPE_MS_BASIC_DATA;
static struct uuid gpt_uuid_ms_ldm_data = GPT_ENT_TYPE_MS_LDM_DATA;
static struct uuid gpt_uuid_ms_ldm_metadata = GPT_ENT_TYPE_MS_LDM_METADATA;
static struct uuid gpt_uuid_ms_recovery = GPT_ENT_TYPE_MS_RECOVERY;
static struct uuid gpt_uuid_ms_reserved = GPT_ENT_TYPE_MS_RESERVED;
static struct uuid gpt_uuid_ms_spaces = GPT_ENT_TYPE_MS_SPACES;
static struct uuid gpt_uuid_netbsd_ccd = GPT_ENT_TYPE_NETBSD_CCD;
static struct uuid gpt_uuid_netbsd_cgd = GPT_ENT_TYPE_NETBSD_CGD;
static struct uuid gpt_uuid_netbsd_ffs = GPT_ENT_TYPE_NETBSD_FFS;
static struct uuid gpt_uuid_netbsd_lfs = GPT_ENT_TYPE_NETBSD_LFS;
static struct uuid gpt_uuid_netbsd_raid = GPT_ENT_TYPE_NETBSD_RAID;
static struct uuid gpt_uuid_netbsd_swap = GPT_ENT_TYPE_NETBSD_SWAP;
static struct uuid gpt_uuid_openbsd_data = GPT_ENT_TYPE_OPENBSD_DATA;
static struct uuid gpt_uuid_prep_boot = GPT_ENT_TYPE_PREP_BOOT;
static struct uuid gpt_uuid_unused = GPT_ENT_TYPE_UNUSED;
static struct uuid gpt_uuid_vmfs = GPT_ENT_TYPE_VMFS;
static struct uuid gpt_uuid_vmkdiag = GPT_ENT_TYPE_VMKDIAG;
static struct uuid gpt_uuid_vmreserved = GPT_ENT_TYPE_VMRESERVED;
static struct uuid gpt_uuid_vmvsanhdr = GPT_ENT_TYPE_VMVSANHDR;

static struct g_part_uuid_alias {
	struct uuid *uuid;
	int alias;
	int mbrtype;
} gpt_uuid_alias_match[] = {
	{ &gpt_uuid_apple_apfs,		G_PART_ALIAS_APPLE_APFS,	 0 },
	{ &gpt_uuid_apple_boot,		G_PART_ALIAS_APPLE_BOOT,	 0xab },
	{ &gpt_uuid_apple_core_storage,	G_PART_ALIAS_APPLE_CORE_STORAGE, 0 },
	{ &gpt_uuid_apple_hfs,		G_PART_ALIAS_APPLE_HFS,		 0xaf },
	{ &gpt_uuid_apple_label,	G_PART_ALIAS_APPLE_LABEL,	 0 },
	{ &gpt_uuid_apple_raid,		G_PART_ALIAS_APPLE_RAID,	 0 },
	{ &gpt_uuid_apple_raid_offline,	G_PART_ALIAS_APPLE_RAID_OFFLINE, 0 },
	{ &gpt_uuid_apple_tv_recovery,	G_PART_ALIAS_APPLE_TV_RECOVERY,	 0 },
	{ &gpt_uuid_apple_ufs,		G_PART_ALIAS_APPLE_UFS,		 0 },
	{ &gpt_uuid_bios_boot,		G_PART_ALIAS_BIOS_BOOT,		 0 },
	{ &gpt_uuid_chromeos_firmware,	G_PART_ALIAS_CHROMEOS_FIRMWARE,	 0 },
	{ &gpt_uuid_chromeos_kernel,	G_PART_ALIAS_CHROMEOS_KERNEL,	 0 },
	{ &gpt_uuid_chromeos_reserved,	G_PART_ALIAS_CHROMEOS_RESERVED,	 0 },
	{ &gpt_uuid_chromeos_root,	G_PART_ALIAS_CHROMEOS_ROOT,	 0 },
	{ &gpt_uuid_dfbsd_ccd,		G_PART_ALIAS_DFBSD_CCD,		 0 },
	{ &gpt_uuid_dfbsd_hammer,	G_PART_ALIAS_DFBSD_HAMMER,	 0 },
	{ &gpt_uuid_dfbsd_hammer2,	G_PART_ALIAS_DFBSD_HAMMER2,	 0 },
	{ &gpt_uuid_dfbsd_label32,	G_PART_ALIAS_DFBSD,		 0xa5 },
	{ &gpt_uuid_dfbsd_label64,	G_PART_ALIAS_DFBSD64,		 0xa5 },
	{ &gpt_uuid_dfbsd_legacy,	G_PART_ALIAS_DFBSD_LEGACY,	 0 },
	{ &gpt_uuid_dfbsd_swap,		G_PART_ALIAS_DFBSD_SWAP,	 0 },
	{ &gpt_uuid_dfbsd_ufs1,		G_PART_ALIAS_DFBSD_UFS,		 0 },
	{ &gpt_uuid_dfbsd_vinum,	G_PART_ALIAS_DFBSD_VINUM,	 0 },
	{ &gpt_uuid_efi, 		G_PART_ALIAS_EFI,		 0xee },
	{ &gpt_uuid_freebsd,		G_PART_ALIAS_FREEBSD,		 0xa5 },
	{ &gpt_uuid_freebsd_boot, 	G_PART_ALIAS_FREEBSD_BOOT,	 0 },
	{ &gpt_uuid_freebsd_nandfs, 	G_PART_ALIAS_FREEBSD_NANDFS,	 0 },
	{ &gpt_uuid_freebsd_swap,	G_PART_ALIAS_FREEBSD_SWAP,	 0 },
	{ &gpt_uuid_freebsd_ufs,	G_PART_ALIAS_FREEBSD_UFS,	 0 },
	{ &gpt_uuid_freebsd_vinum,	G_PART_ALIAS_FREEBSD_VINUM,	 0 },
	{ &gpt_uuid_freebsd_zfs,	G_PART_ALIAS_FREEBSD_ZFS,	 0 },
	{ &gpt_uuid_linux_data,		G_PART_ALIAS_LINUX_DATA,	 0x0b },
	{ &gpt_uuid_linux_lvm,		G_PART_ALIAS_LINUX_LVM,		 0 },
	{ &gpt_uuid_linux_raid,		G_PART_ALIAS_LINUX_RAID,	 0 },
	{ &gpt_uuid_linux_swap,		G_PART_ALIAS_LINUX_SWAP,	 0 },
	{ &gpt_uuid_mbr,		G_PART_ALIAS_MBR,		 0 },
	{ &gpt_uuid_ms_basic_data,	G_PART_ALIAS_MS_BASIC_DATA,	 0x0b },
	{ &gpt_uuid_ms_ldm_data,	G_PART_ALIAS_MS_LDM_DATA,	 0 },
	{ &gpt_uuid_ms_ldm_metadata,	G_PART_ALIAS_MS_LDM_METADATA,	 0 },
	{ &gpt_uuid_ms_recovery,	G_PART_ALIAS_MS_RECOVERY,	 0 },
	{ &gpt_uuid_ms_reserved,	G_PART_ALIAS_MS_RESERVED,	 0 },
	{ &gpt_uuid_ms_spaces,		G_PART_ALIAS_MS_SPACES,		 0 },
	{ &gpt_uuid_netbsd_ccd,		G_PART_ALIAS_NETBSD_CCD,	 0 },
	{ &gpt_uuid_netbsd_cgd,		G_PART_ALIAS_NETBSD_CGD,	 0 },
	{ &gpt_uuid_netbsd_ffs,		G_PART_ALIAS_NETBSD_FFS,	 0 },
	{ &gpt_uuid_netbsd_lfs,		G_PART_ALIAS_NETBSD_LFS,	 0 },
	{ &gpt_uuid_netbsd_raid,	G_PART_ALIAS_NETBSD_RAID,	 0 },
	{ &gpt_uuid_netbsd_swap,	G_PART_ALIAS_NETBSD_SWAP,	 0 },
	{ &gpt_uuid_openbsd_data,	G_PART_ALIAS_OPENBSD_DATA,	 0 },
	{ &gpt_uuid_prep_boot,		G_PART_ALIAS_PREP_BOOT,		 0x41 },
	{ &gpt_uuid_vmfs,		G_PART_ALIAS_VMFS,		 0 },
	{ &gpt_uuid_vmkdiag,		G_PART_ALIAS_VMKDIAG,		 0 },
	{ &gpt_uuid_vmreserved,		G_PART_ALIAS_VMRESERVED,	 0 },
	{ &gpt_uuid_vmvsanhdr,		G_PART_ALIAS_VMVSANHDR,		 0 },
	{ NULL, 0, 0 }
};

static int
gpt_write_mbr_entry(u_char *mbr, int idx, int typ, quad_t start,
    quad_t end)
{

	if (typ == 0 || start > UINT32_MAX || end > UINT32_MAX)
		return (EINVAL);

	mbr += DOSPARTOFF + idx * DOSPARTSIZE;
	mbr[0] = 0;
	if (start == 1) {
		/*
		 * Treat the PMBR partition specially to maximize
		 * interoperability with BIOSes.
		 */
		mbr[1] = mbr[3] = 0;
		mbr[2] = 2;
	} else
		mbr[1] = mbr[2] = mbr[3] = 0xff;
	mbr[4] = typ;
	mbr[5] = mbr[6] = mbr[7] = 0xff;
	le32enc(mbr + 8, (uint32_t)start);
	le32enc(mbr + 12, (uint32_t)(end - start + 1));
	return (0);
}

static int
gpt_map_type(struct uuid *t)
{
	struct g_part_uuid_alias *uap;

	for (uap = &gpt_uuid_alias_match[0]; uap->uuid; uap++) {
		if (EQUUID(t, uap->uuid))
			return (uap->mbrtype);
	}
	return (0);
}

static void
gpt_create_pmbr(struct g_part_gpt_table *table, struct g_provider *pp)
{

	bzero(table->mbr + DOSPARTOFF, DOSPARTSIZE * NDOSPART);
	gpt_write_mbr_entry(table->mbr, 0, 0xee, 1,
	    MIN(pp->mediasize / pp->sectorsize - 1, UINT32_MAX));
	le16enc(table->mbr + DOSMAGICOFFSET, DOSMAGIC);
}

/*
 * Under Boot Camp the PMBR partition (type 0xEE) doesn't cover the
 * whole disk anymore. Rather, it covers the GPT table and the EFI
 * system partition only. This way the HFS+ partition and any FAT
 * partitions can be added to the MBR without creating an overlap.
 */
static int
gpt_is_bootcamp(struct g_part_gpt_table *table, const char *provname)
{
	uint8_t *p;

	p = table->mbr + DOSPARTOFF;
	if (p[4] != 0xee || le32dec(p + 8) != 1)
		return (0);

	p += DOSPARTSIZE;
	if (p[4] != 0xaf)
		return (0);

	printf("GEOM: %s: enabling Boot Camp\n", provname);
	return (1);
}

static void
gpt_update_bootcamp(struct g_part_table *basetable, struct g_provider *pp)
{
	struct g_part_entry *baseentry;
	struct g_part_gpt_entry *entry;
	struct g_part_gpt_table *table;
	int bootable, error, index, slices, typ;

	table = (struct g_part_gpt_table *)basetable;

	bootable = -1;
	for (index = 0; index < NDOSPART; index++) {
		if (table->mbr[DOSPARTOFF + DOSPARTSIZE * index])
			bootable = index;
	}

	bzero(table->mbr + DOSPARTOFF, DOSPARTSIZE * NDOSPART);
	slices = 0;
	LIST_FOREACH(baseentry, &basetable->gpt_entry, gpe_entry) {
		if (baseentry->gpe_deleted)
			continue;
		index = baseentry->gpe_index - 1;
		if (index >= NDOSPART)
			continue;

		entry = (struct g_part_gpt_entry *)baseentry;

		switch (index) {
		case 0:	/* This must be the EFI system partition. */
			if (!EQUUID(&entry->ent.ent_type, &gpt_uuid_efi))
				goto disable;
			error = gpt_write_mbr_entry(table->mbr, index, 0xee,
			    1ull, entry->ent.ent_lba_end);
			break;
		case 1:	/* This must be the HFS+ partition. */
			if (!EQUUID(&entry->ent.ent_type, &gpt_uuid_apple_hfs))
				goto disable;
			error = gpt_write_mbr_entry(table->mbr, index, 0xaf,
			    entry->ent.ent_lba_start, entry->ent.ent_lba_end);
			break;
		default:
			typ = gpt_map_type(&entry->ent.ent_type);
			error = gpt_write_mbr_entry(table->mbr, index, typ,
			    entry->ent.ent_lba_start, entry->ent.ent_lba_end);
			break;
		}
		if (error)
			continue;

		if (index == bootable)
			table->mbr[DOSPARTOFF + DOSPARTSIZE * index] = 0x80;
		slices |= 1 << index;
	}
	if ((slices & 3) == 3)
		return;

 disable:
	table->bootcamp = 0;
	gpt_create_pmbr(table, pp);
}

static struct gpt_hdr *
gpt_read_hdr(struct g_part_gpt_table *table, struct g_consumer *cp,
    enum gpt_elt elt)
{
	struct gpt_hdr *buf, *hdr;
	struct g_provider *pp;
	quad_t lba, last;
	int error;
	uint32_t crc, sz;

	pp = cp->provider;
	last = (pp->mediasize / pp->sectorsize) - 1;
	table->state[elt] = GPT_STATE_MISSING;
	/*
	 * If the primary header is valid look for secondary
	 * header in AlternateLBA, otherwise in the last medium's LBA.
	 */
	if (elt == GPT_ELT_SECHDR) {
		if (table->state[GPT_ELT_PRIHDR] != GPT_STATE_OK)
			table->lba[elt] = last;
	} else
		table->lba[elt] = 1;
	buf = g_read_data(cp, table->lba[elt] * pp->sectorsize, pp->sectorsize,
	    &error);
	if (buf == NULL)
		return (NULL);
	hdr = NULL;
	if (memcmp(buf->hdr_sig, GPT_HDR_SIG, sizeof(buf->hdr_sig)) != 0)
		goto fail;

	table->state[elt] = GPT_STATE_CORRUPT;
	sz = le32toh(buf->hdr_size);
	if (sz < 92 || sz > pp->sectorsize)
		goto fail;

	hdr = g_malloc(sz, M_WAITOK | M_ZERO);
	bcopy(buf, hdr, sz);
	hdr->hdr_size = sz;

	crc = le32toh(buf->hdr_crc_self);
	buf->hdr_crc_self = 0;
	if (crc32(buf, sz) != crc)
		goto fail;
	hdr->hdr_crc_self = crc;

	table->state[elt] = GPT_STATE_INVALID;
	hdr->hdr_revision = le32toh(buf->hdr_revision);
	if (hdr->hdr_revision < GPT_HDR_REVISION)
		goto fail;
	hdr->hdr_lba_self = le64toh(buf->hdr_lba_self);
	if (hdr->hdr_lba_self != table->lba[elt])
		goto fail;
	hdr->hdr_lba_alt = le64toh(buf->hdr_lba_alt);
	if (hdr->hdr_lba_alt == hdr->hdr_lba_self ||
	    hdr->hdr_lba_alt > last)
		goto fail;

	/* Check the managed area. */
	hdr->hdr_lba_start = le64toh(buf->hdr_lba_start);
	if (hdr->hdr_lba_start < 2 || hdr->hdr_lba_start >= last)
		goto fail;
	hdr->hdr_lba_end = le64toh(buf->hdr_lba_end);
	if (hdr->hdr_lba_end < hdr->hdr_lba_start || hdr->hdr_lba_end >= last)
		goto fail;

	/* Check the table location and size of the table. */
	hdr->hdr_entries = le32toh(buf->hdr_entries);
	hdr->hdr_entsz = le32toh(buf->hdr_entsz);
	if (hdr->hdr_entries == 0 || hdr->hdr_entsz < 128 ||
	    (hdr->hdr_entsz & 7) != 0)
		goto fail;
	hdr->hdr_lba_table = le64toh(buf->hdr_lba_table);
	if (hdr->hdr_lba_table < 2 || hdr->hdr_lba_table >= last)
		goto fail;
	if (hdr->hdr_lba_table >= hdr->hdr_lba_start &&
	    hdr->hdr_lba_table <= hdr->hdr_lba_end)
		goto fail;
	lba = hdr->hdr_lba_table +
	    howmany(hdr->hdr_entries * hdr->hdr_entsz, pp->sectorsize) - 1;
	if (lba >= last)
		goto fail;
	if (lba >= hdr->hdr_lba_start && lba <= hdr->hdr_lba_end)
		goto fail;

	table->state[elt] = GPT_STATE_OK;
	le_uuid_dec(&buf->hdr_uuid, &hdr->hdr_uuid);
	hdr->hdr_crc_table = le32toh(buf->hdr_crc_table);

	/* save LBA for secondary header */
	if (elt == GPT_ELT_PRIHDR)
		table->lba[GPT_ELT_SECHDR] = hdr->hdr_lba_alt;

	g_free(buf);
	return (hdr);

 fail:
	if (hdr != NULL)
		g_free(hdr);
	g_free(buf);
	return (NULL);
}

static struct gpt_ent *
gpt_read_tbl(struct g_part_gpt_table *table, struct g_consumer *cp,
    enum gpt_elt elt, struct gpt_hdr *hdr)
{
	struct g_provider *pp;
	struct gpt_ent *ent, *tbl;
	char *buf, *p;
	unsigned int idx, sectors, tblsz, size;
	int error;

	if (hdr == NULL)
		return (NULL);

	pp = cp->provider;
	table->lba[elt] = hdr->hdr_lba_table;

	table->state[elt] = GPT_STATE_MISSING;
	tblsz = hdr->hdr_entries * hdr->hdr_entsz;
	sectors = howmany(tblsz, pp->sectorsize);
	buf = g_malloc(sectors * pp->sectorsize, M_WAITOK | M_ZERO);
	for (idx = 0; idx < sectors; idx += MAXPHYS / pp->sectorsize) {
		size = (sectors - idx > MAXPHYS / pp->sectorsize) ?  MAXPHYS:
		    (sectors - idx) * pp->sectorsize;
		p = g_read_data(cp, (table->lba[elt] + idx) * pp->sectorsize,
		    size, &error);
		if (p == NULL) {
			g_free(buf);
			return (NULL);
		}
		bcopy(p, buf + idx * pp->sectorsize, size);
		g_free(p);
	}
	table->state[elt] = GPT_STATE_CORRUPT;
	if (crc32(buf, tblsz) != hdr->hdr_crc_table) {
		g_free(buf);
		return (NULL);
	}

	table->state[elt] = GPT_STATE_OK;
	tbl = g_malloc(hdr->hdr_entries * sizeof(struct gpt_ent),
	    M_WAITOK | M_ZERO);

	for (idx = 0, ent = tbl, p = buf;
	     idx < hdr->hdr_entries;
	     idx++, ent++, p += hdr->hdr_entsz) {
		le_uuid_dec(p, &ent->ent_type);
		le_uuid_dec(p + 16, &ent->ent_uuid);
		ent->ent_lba_start = le64dec(p + 32);
		ent->ent_lba_end = le64dec(p + 40);
		ent->ent_attr = le64dec(p + 48);
		/* Keep UTF-16 in little-endian. */
		bcopy(p + 56, ent->ent_name, sizeof(ent->ent_name));
	}

	g_free(buf);
	return (tbl);
}

static int
gpt_matched_hdrs(struct gpt_hdr *pri, struct gpt_hdr *sec)
{

	if (pri == NULL || sec == NULL)
		return (0);

	if (!EQUUID(&pri->hdr_uuid, &sec->hdr_uuid))
		return (0);
	return ((pri->hdr_revision == sec->hdr_revision &&
	    pri->hdr_size == sec->hdr_size &&
	    pri->hdr_lba_start == sec->hdr_lba_start &&
	    pri->hdr_lba_end == sec->hdr_lba_end &&
	    pri->hdr_entries == sec->hdr_entries &&
	    pri->hdr_entsz == sec->hdr_entsz &&
	    pri->hdr_crc_table == sec->hdr_crc_table) ? 1 : 0);
}

static int
gpt_parse_type(const char *type, struct uuid *uuid)
{
	struct uuid tmp;
	const char *alias;
	int error;
	struct g_part_uuid_alias *uap;

	if (type[0] == '!') {
		error = parse_uuid(type + 1, &tmp);
		if (error)
			return (error);
		if (EQUUID(&tmp, &gpt_uuid_unused))
			return (EINVAL);
		*uuid = tmp;
		return (0);
	}
	for (uap = &gpt_uuid_alias_match[0]; uap->uuid; uap++) {
		alias = g_part_alias_name(uap->alias);
		if (!strcasecmp(type, alias)) {
			*uuid = *uap->uuid;
			return (0);
		}
	}
	return (EINVAL);
}

static int
g_part_gpt_add(struct g_part_table *basetable, struct g_part_entry *baseentry,
    struct g_part_parms *gpp)
{
	struct g_part_gpt_entry *entry;
	int error;

	entry = (struct g_part_gpt_entry *)baseentry;
	error = gpt_parse_type(gpp->gpp_type, &entry->ent.ent_type);
	if (error)
		return (error);
	kern_uuidgen(&entry->ent.ent_uuid, 1);
	entry->ent.ent_lba_start = baseentry->gpe_start;
	entry->ent.ent_lba_end = baseentry->gpe_end;
	if (baseentry->gpe_deleted) {
		entry->ent.ent_attr = 0;
		bzero(entry->ent.ent_name, sizeof(entry->ent.ent_name));
	}
	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		g_gpt_utf8_to_utf16(gpp->gpp_label, entry->ent.ent_name,
		    sizeof(entry->ent.ent_name) /
		    sizeof(entry->ent.ent_name[0]));
	return (0);
}

static int
g_part_gpt_bootcode(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_part_gpt_table *table;
	size_t codesz;

	codesz = DOSPARTOFF;
	table = (struct g_part_gpt_table *)basetable;
	bzero(table->mbr, codesz);
	codesz = MIN(codesz, gpp->gpp_codesize);
	if (codesz > 0)
		bcopy(gpp->gpp_codeptr, table->mbr, codesz);
	return (0);
}

static int
g_part_gpt_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_provider *pp;
	struct g_part_gpt_table *table;
	size_t tblsz;

	/* We don't nest, which means that our depth should be 0. */
	if (basetable->gpt_depth != 0)
		return (ENXIO);

	table = (struct g_part_gpt_table *)basetable;
	pp = gpp->gpp_provider;
	tblsz = howmany(basetable->gpt_entries * sizeof(struct gpt_ent),
	    pp->sectorsize);
	if (pp->sectorsize < MBRSIZE ||
	    pp->mediasize < (3 + 2 * tblsz + basetable->gpt_entries) *
	    pp->sectorsize)
		return (ENOSPC);

	gpt_create_pmbr(table, pp);

	/* Allocate space for the header */
	table->hdr = g_malloc(sizeof(struct gpt_hdr), M_WAITOK | M_ZERO);

	bcopy(GPT_HDR_SIG, table->hdr->hdr_sig, sizeof(table->hdr->hdr_sig));
	table->hdr->hdr_revision = GPT_HDR_REVISION;
	table->hdr->hdr_size = offsetof(struct gpt_hdr, padding);
	kern_uuidgen(&table->hdr->hdr_uuid, 1);
	table->hdr->hdr_entries = basetable->gpt_entries;
	table->hdr->hdr_entsz = sizeof(struct gpt_ent);

	g_gpt_set_defaults(basetable, pp);
	return (0);
}

static int
g_part_gpt_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_part_gpt_table *table;
	struct g_provider *pp;

	table = (struct g_part_gpt_table *)basetable;
	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	g_free(table->hdr);
	table->hdr = NULL;

	/*
	 * Wipe the first 2 sectors and last one to clear the partitioning.
	 * Wipe sectors only if they have valid metadata.
	 */
	if (table->state[GPT_ELT_PRIHDR] == GPT_STATE_OK)
		basetable->gpt_smhead |= 3;
	if (table->state[GPT_ELT_SECHDR] == GPT_STATE_OK &&
	    table->lba[GPT_ELT_SECHDR] == pp->mediasize / pp->sectorsize - 1)
		basetable->gpt_smtail |= 1;
	return (0);
}

static void
g_part_gpt_dumpconf(struct g_part_table *table, struct g_part_entry *baseentry,
    struct sbuf *sb, const char *indent)
{
	struct g_part_gpt_entry *entry;

	entry = (struct g_part_gpt_entry *)baseentry;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs GPT xt ");
		sbuf_printf_uuid(sb, &entry->ent.ent_type);
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		sbuf_printf(sb, "%s<label>", indent);
		g_gpt_printf_utf16(sb, entry->ent.ent_name,
		    sizeof(entry->ent.ent_name) >> 1);
		sbuf_printf(sb, "</label>\n");
		if (entry->ent.ent_attr & GPT_ENT_ATTR_BOOTME)
			sbuf_printf(sb, "%s<attrib>bootme</attrib>\n", indent);
		if (entry->ent.ent_attr & GPT_ENT_ATTR_BOOTONCE) {
			sbuf_printf(sb, "%s<attrib>bootonce</attrib>\n",
			    indent);
		}
		if (entry->ent.ent_attr & GPT_ENT_ATTR_BOOTFAILED) {
			sbuf_printf(sb, "%s<attrib>bootfailed</attrib>\n",
			    indent);
		}
		sbuf_printf(sb, "%s<rawtype>", indent);
		sbuf_printf_uuid(sb, &entry->ent.ent_type);
		sbuf_printf(sb, "</rawtype>\n");
		sbuf_printf(sb, "%s<rawuuid>", indent);
		sbuf_printf_uuid(sb, &entry->ent.ent_uuid);
		sbuf_printf(sb, "</rawuuid>\n");
		sbuf_printf(sb, "%s<efimedia>", indent);
		sbuf_printf(sb, "HD(%d,GPT,", entry->base.gpe_index);
		sbuf_printf_uuid(sb, &entry->ent.ent_uuid);
		sbuf_printf(sb, ",%#jx,%#jx)", (intmax_t)entry->base.gpe_start,
		    (intmax_t)(entry->base.gpe_end - entry->base.gpe_start + 1));
		sbuf_printf(sb, "</efimedia>\n");
	} else {
		/* confxml: scheme information */
	}
}

static int
g_part_gpt_dumpto(struct g_part_table *table, struct g_part_entry *baseentry)
{
	struct g_part_gpt_entry *entry;

	entry = (struct g_part_gpt_entry *)baseentry;
	return ((EQUUID(&entry->ent.ent_type, &gpt_uuid_freebsd_swap) ||
	    EQUUID(&entry->ent.ent_type, &gpt_uuid_linux_swap) ||
	    EQUUID(&entry->ent.ent_type, &gpt_uuid_dfbsd_swap)) ? 1 : 0);
}

static int
g_part_gpt_modify(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_gpt_entry *entry;
	int error;

	entry = (struct g_part_gpt_entry *)baseentry;
	if (gpp->gpp_parms & G_PART_PARM_TYPE) {
		error = gpt_parse_type(gpp->gpp_type, &entry->ent.ent_type);
		if (error)
			return (error);
	}
	if (gpp->gpp_parms & G_PART_PARM_LABEL)
		g_gpt_utf8_to_utf16(gpp->gpp_label, entry->ent.ent_name,
		    sizeof(entry->ent.ent_name) /
		    sizeof(entry->ent.ent_name[0]));
	return (0);
}

static int
g_part_gpt_resize(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{
	struct g_part_gpt_entry *entry;

	if (baseentry == NULL)
		return (g_part_gpt_recover(basetable));

	entry = (struct g_part_gpt_entry *)baseentry;
	baseentry->gpe_end = baseentry->gpe_start + gpp->gpp_size - 1;
	entry->ent.ent_lba_end = baseentry->gpe_end;

	return (0);
}

static const char *
g_part_gpt_name(struct g_part_table *table, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_gpt_entry *entry;
	char c;

	entry = (struct g_part_gpt_entry *)baseentry;
	c = (EQUUID(&entry->ent.ent_type, &gpt_uuid_freebsd)) ? 's' : 'p';
	snprintf(buf, bufsz, "%c%d", c, baseentry->gpe_index);
	return (buf);
}

static int
g_part_gpt_probe(struct g_part_table *table, struct g_consumer *cp)
{
	struct g_provider *pp;
	u_char *buf;
	int error, index, pri, res;

	/* We don't nest, which means that our depth should be 0. */
	if (table->gpt_depth != 0)
		return (ENXIO);

	pp = cp->provider;

	/*
	 * Sanity-check the provider. Since the first sector on the provider
	 * must be a PMBR and a PMBR is 512 bytes large, the sector size
	 * must be at least 512 bytes.  Also, since the theoretical minimum
	 * number of sectors needed by GPT is 6, any medium that has less
	 * than 6 sectors is never going to be able to hold a GPT. The
	 * number 6 comes from:
	 *	1 sector for the PMBR
	 *	2 sectors for the GPT headers (each 1 sector)
	 *	2 sectors for the GPT tables (each 1 sector)
	 *	1 sector for an actual partition
	 * It's better to catch this pathological case early than behaving
	 * pathologically later on...
	 */
	if (pp->sectorsize < MBRSIZE || pp->mediasize < 6 * pp->sectorsize)
		return (ENOSPC);

	/*
	 * Check that there's a MBR or a PMBR. If it's a PMBR, we return
	 * as the highest priority on a match, otherwise we assume some
	 * GPT-unaware tool has destroyed the GPT by recreating a MBR and
	 * we really want the MBR scheme to take precedence.
	 */
	buf = g_read_data(cp, 0L, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);
	res = le16dec(buf + DOSMAGICOFFSET);
	pri = G_PART_PROBE_PRI_LOW;
	if (res == DOSMAGIC) {
		for (index = 0; index < NDOSPART; index++) {
			if (buf[DOSPARTOFF + DOSPARTSIZE * index + 4] == 0xee)
				pri = G_PART_PROBE_PRI_HIGH;
		}
		g_free(buf);

		/* Check that there's a primary header. */
		buf = g_read_data(cp, pp->sectorsize, pp->sectorsize, &error);
		if (buf == NULL)
			return (error);
		res = memcmp(buf, GPT_HDR_SIG, 8);
		g_free(buf);
		if (res == 0)
			return (pri);
	} else
		g_free(buf);

	/* No primary? Check that there's a secondary. */
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize, pp->sectorsize,
	    &error);
	if (buf == NULL)
		return (error);
	res = memcmp(buf, GPT_HDR_SIG, 8);
	g_free(buf);
	return ((res == 0) ? pri : ENXIO);
}

static int
g_part_gpt_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct gpt_hdr *prihdr, *sechdr;
	struct gpt_ent *tbl, *pritbl, *sectbl;
	struct g_provider *pp;
	struct g_part_gpt_table *table;
	struct g_part_gpt_entry *entry;
	u_char *buf;
	uint64_t last;
	int error, index;

	table = (struct g_part_gpt_table *)basetable;
	pp = cp->provider;
	last = (pp->mediasize / pp->sectorsize) - 1;

	/* Read the PMBR */
	buf = g_read_data(cp, 0, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);
	bcopy(buf, table->mbr, MBRSIZE);
	g_free(buf);

	/* Read the primary header and table. */
	prihdr = gpt_read_hdr(table, cp, GPT_ELT_PRIHDR);
	if (table->state[GPT_ELT_PRIHDR] == GPT_STATE_OK) {
		pritbl = gpt_read_tbl(table, cp, GPT_ELT_PRITBL, prihdr);
	} else {
		table->state[GPT_ELT_PRITBL] = GPT_STATE_MISSING;
		pritbl = NULL;
	}

	/* Read the secondary header and table. */
	sechdr = gpt_read_hdr(table, cp, GPT_ELT_SECHDR);
	if (table->state[GPT_ELT_SECHDR] == GPT_STATE_OK) {
		sectbl = gpt_read_tbl(table, cp, GPT_ELT_SECTBL, sechdr);
	} else {
		table->state[GPT_ELT_SECTBL] = GPT_STATE_MISSING;
		sectbl = NULL;
	}

	/* Fail if we haven't got any good tables at all. */
	if (table->state[GPT_ELT_PRITBL] != GPT_STATE_OK &&
	    table->state[GPT_ELT_SECTBL] != GPT_STATE_OK) {
		printf("GEOM: %s: corrupt or invalid GPT detected.\n",
		    pp->name);
		printf("GEOM: %s: GPT rejected -- may not be recoverable.\n",
		    pp->name);
		if (prihdr != NULL)
			g_free(prihdr);
		if (pritbl != NULL)
			g_free(pritbl);
		if (sechdr != NULL)
			g_free(sechdr);
		if (sectbl != NULL)
			g_free(sectbl);
		return (EINVAL);
	}

	/*
	 * If both headers are good but they disagree with each other,
	 * then invalidate one. We prefer to keep the primary header,
	 * unless the primary table is corrupt.
	 */
	if (table->state[GPT_ELT_PRIHDR] == GPT_STATE_OK &&
	    table->state[GPT_ELT_SECHDR] == GPT_STATE_OK &&
	    !gpt_matched_hdrs(prihdr, sechdr)) {
		if (table->state[GPT_ELT_PRITBL] == GPT_STATE_OK) {
			table->state[GPT_ELT_SECHDR] = GPT_STATE_INVALID;
			table->state[GPT_ELT_SECTBL] = GPT_STATE_MISSING;
			g_free(sechdr);
			sechdr = NULL;
		} else {
			table->state[GPT_ELT_PRIHDR] = GPT_STATE_INVALID;
			table->state[GPT_ELT_PRITBL] = GPT_STATE_MISSING;
			g_free(prihdr);
			prihdr = NULL;
		}
	}

	if (table->state[GPT_ELT_PRITBL] != GPT_STATE_OK) {
		printf("GEOM: %s: the primary GPT table is corrupt or "
		    "invalid.\n", pp->name);
		printf("GEOM: %s: using the secondary instead -- recovery "
		    "strongly advised.\n", pp->name);
		table->hdr = sechdr;
		basetable->gpt_corrupt = 1;
		if (prihdr != NULL)
			g_free(prihdr);
		tbl = sectbl;
		if (pritbl != NULL)
			g_free(pritbl);
	} else {
		if (table->state[GPT_ELT_SECTBL] != GPT_STATE_OK) {
			printf("GEOM: %s: the secondary GPT table is corrupt "
			    "or invalid.\n", pp->name);
			printf("GEOM: %s: using the primary only -- recovery "
			    "suggested.\n", pp->name);
			basetable->gpt_corrupt = 1;
		} else if (table->lba[GPT_ELT_SECHDR] != last) {
			printf( "GEOM: %s: the secondary GPT header is not in "
			    "the last LBA.\n", pp->name);
			basetable->gpt_corrupt = 1;
		}
		table->hdr = prihdr;
		if (sechdr != NULL)
			g_free(sechdr);
		tbl = pritbl;
		if (sectbl != NULL)
			g_free(sectbl);
	}

	basetable->gpt_first = table->hdr->hdr_lba_start;
	basetable->gpt_last = table->hdr->hdr_lba_end;
	basetable->gpt_entries = table->hdr->hdr_entries;

	for (index = basetable->gpt_entries - 1; index >= 0; index--) {
		if (EQUUID(&tbl[index].ent_type, &gpt_uuid_unused))
			continue;
		entry = (struct g_part_gpt_entry *)g_part_new_entry(
		    basetable, index + 1, tbl[index].ent_lba_start,
		    tbl[index].ent_lba_end);
		entry->ent = tbl[index];
	}

	g_free(tbl);

	/*
	 * Under Mac OS X, the MBR mirrors the first 4 GPT partitions
	 * if (and only if) any FAT32 or FAT16 partitions have been
	 * created. This happens irrespective of whether Boot Camp is
	 * used/enabled, though it's generally understood to be done
	 * to support legacy Windows under Boot Camp. We refer to this
	 * mirroring simply as Boot Camp. We try to detect Boot Camp
	 * so that we can update the MBR if and when GPT changes have
	 * been made. Note that we do not enable Boot Camp if not
	 * previously enabled because we can't assume that we're on a
	 * Mac alongside Mac OS X.
	 */
	table->bootcamp = gpt_is_bootcamp(table, pp->name);

	return (0);
}

static int
g_part_gpt_recover(struct g_part_table *basetable)
{
	struct g_part_gpt_table *table;
	struct g_provider *pp;

	table = (struct g_part_gpt_table *)basetable;
	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	gpt_create_pmbr(table, pp);
	g_gpt_set_defaults(basetable, pp);
	basetable->gpt_corrupt = 0;
	return (0);
}

static int
g_part_gpt_setunset(struct g_part_table *basetable,
    struct g_part_entry *baseentry, const char *attrib, unsigned int set)
{
	struct g_part_gpt_entry *entry;
	struct g_part_gpt_table *table;
	struct g_provider *pp;
	uint8_t *p;
	uint64_t attr;
	int i;

	table = (struct g_part_gpt_table *)basetable;
	entry = (struct g_part_gpt_entry *)baseentry;

	if (strcasecmp(attrib, "active") == 0) {
		if (table->bootcamp) {
			/* The active flag must be set on a valid entry. */
			if (entry == NULL)
				return (ENXIO);
			if (baseentry->gpe_index > NDOSPART)
				return (EINVAL);
			for (i = 0; i < NDOSPART; i++) {
				p = &table->mbr[DOSPARTOFF + i * DOSPARTSIZE];
				p[0] = (i == baseentry->gpe_index - 1)
				    ? ((set) ? 0x80 : 0) : 0;
			}
		} else {
			/* The PMBR is marked as active without an entry. */
			if (entry != NULL)
				return (ENXIO);
			for (i = 0; i < NDOSPART; i++) {
				p = &table->mbr[DOSPARTOFF + i * DOSPARTSIZE];
				p[0] = (p[4] == 0xee) ? ((set) ? 0x80 : 0) : 0;
			}
		}
		return (0);
	} else if (strcasecmp(attrib, "lenovofix") == 0) {
		/*
		 * Write the 0xee GPT entry to slot #1 (2nd slot) in the pMBR.
		 * This workaround allows Lenovo X220, T420, T520, etc to boot
		 * from GPT Partitions in BIOS mode.
		 */

		if (entry != NULL)
			return (ENXIO);

		pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
		bzero(table->mbr + DOSPARTOFF, DOSPARTSIZE * NDOSPART);
		gpt_write_mbr_entry(table->mbr, ((set) ? 1 : 0), 0xee, 1,
		    MIN(pp->mediasize / pp->sectorsize - 1, UINT32_MAX));
		return (0);
	}

	if (entry == NULL)
		return (ENODEV);

	attr = 0;
	if (strcasecmp(attrib, "bootme") == 0) {
		attr |= GPT_ENT_ATTR_BOOTME;
	} else if (strcasecmp(attrib, "bootonce") == 0) {
		attr |= GPT_ENT_ATTR_BOOTONCE;
		if (set)
			attr |= GPT_ENT_ATTR_BOOTME;
	} else if (strcasecmp(attrib, "bootfailed") == 0) {
		/*
		 * It should only be possible to unset BOOTFAILED, but it might
		 * be useful for test purposes to also be able to set it.
		 */
		attr |= GPT_ENT_ATTR_BOOTFAILED;
	}
	if (attr == 0)
		return (EINVAL);

	if (set)
		attr = entry->ent.ent_attr | attr;
	else
		attr = entry->ent.ent_attr & ~attr;
	if (attr != entry->ent.ent_attr) {
		entry->ent.ent_attr = attr;
		if (!baseentry->gpe_created)
			baseentry->gpe_modified = 1;
	}
	return (0);
}

static const char *
g_part_gpt_type(struct g_part_table *basetable, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_gpt_entry *entry;
	struct uuid *type;
	struct g_part_uuid_alias *uap;

	entry = (struct g_part_gpt_entry *)baseentry;
	type = &entry->ent.ent_type;
	for (uap = &gpt_uuid_alias_match[0]; uap->uuid; uap++)
		if (EQUUID(type, uap->uuid))
			return (g_part_alias_name(uap->alias));
	buf[0] = '!';
	snprintf_uuid(buf + 1, bufsz - 1, type);

	return (buf);
}

static int
g_part_gpt_write(struct g_part_table *basetable, struct g_consumer *cp)
{
	unsigned char *buf, *bp;
	struct g_provider *pp;
	struct g_part_entry *baseentry;
	struct g_part_gpt_entry *entry;
	struct g_part_gpt_table *table;
	size_t tblsz;
	uint32_t crc;
	int error, index;

	pp = cp->provider;
	table = (struct g_part_gpt_table *)basetable;
	tblsz = howmany(table->hdr->hdr_entries * table->hdr->hdr_entsz,
	    pp->sectorsize);

	/* Reconstruct the MBR from the GPT if under Boot Camp. */
	if (table->bootcamp)
		gpt_update_bootcamp(basetable, pp);

	/* Write the PMBR */
	buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
	bcopy(table->mbr, buf, MBRSIZE);
	error = g_write_data(cp, 0, buf, pp->sectorsize);
	g_free(buf);
	if (error)
		return (error);

	/* Allocate space for the header and entries. */
	buf = g_malloc((tblsz + 1) * pp->sectorsize, M_WAITOK | M_ZERO);

	memcpy(buf, table->hdr->hdr_sig, sizeof(table->hdr->hdr_sig));
	le32enc(buf + 8, table->hdr->hdr_revision);
	le32enc(buf + 12, table->hdr->hdr_size);
	le64enc(buf + 40, table->hdr->hdr_lba_start);
	le64enc(buf + 48, table->hdr->hdr_lba_end);
	le_uuid_enc(buf + 56, &table->hdr->hdr_uuid);
	le32enc(buf + 80, table->hdr->hdr_entries);
	le32enc(buf + 84, table->hdr->hdr_entsz);

	LIST_FOREACH(baseentry, &basetable->gpt_entry, gpe_entry) {
		if (baseentry->gpe_deleted)
			continue;
		entry = (struct g_part_gpt_entry *)baseentry;
		index = baseentry->gpe_index - 1;
		bp = buf + pp->sectorsize + table->hdr->hdr_entsz * index;
		le_uuid_enc(bp, &entry->ent.ent_type);
		le_uuid_enc(bp + 16, &entry->ent.ent_uuid);
		le64enc(bp + 32, entry->ent.ent_lba_start);
		le64enc(bp + 40, entry->ent.ent_lba_end);
		le64enc(bp + 48, entry->ent.ent_attr);
		memcpy(bp + 56, entry->ent.ent_name,
		    sizeof(entry->ent.ent_name));
	}

	crc = crc32(buf + pp->sectorsize,
	    table->hdr->hdr_entries * table->hdr->hdr_entsz);
	le32enc(buf + 88, crc);

	/* Write primary meta-data. */
	le32enc(buf + 16, 0);	/* hdr_crc_self. */
	le64enc(buf + 24, table->lba[GPT_ELT_PRIHDR]);	/* hdr_lba_self. */
	le64enc(buf + 32, table->lba[GPT_ELT_SECHDR]);	/* hdr_lba_alt. */
	le64enc(buf + 72, table->lba[GPT_ELT_PRITBL]);	/* hdr_lba_table. */
	crc = crc32(buf, table->hdr->hdr_size);
	le32enc(buf + 16, crc);

	for (index = 0; index < tblsz; index += MAXPHYS / pp->sectorsize) {
		error = g_write_data(cp,
		    (table->lba[GPT_ELT_PRITBL] + index) * pp->sectorsize,
		    buf + (index + 1) * pp->sectorsize,
		    (tblsz - index > MAXPHYS / pp->sectorsize) ? MAXPHYS:
		    (tblsz - index) * pp->sectorsize);
		if (error)
			goto out;
	}
	error = g_write_data(cp, table->lba[GPT_ELT_PRIHDR] * pp->sectorsize,
	    buf, pp->sectorsize);
	if (error)
		goto out;

	/* Write secondary meta-data. */
	le32enc(buf + 16, 0);	/* hdr_crc_self. */
	le64enc(buf + 24, table->lba[GPT_ELT_SECHDR]);	/* hdr_lba_self. */
	le64enc(buf + 32, table->lba[GPT_ELT_PRIHDR]);	/* hdr_lba_alt. */
	le64enc(buf + 72, table->lba[GPT_ELT_SECTBL]);	/* hdr_lba_table. */
	crc = crc32(buf, table->hdr->hdr_size);
	le32enc(buf + 16, crc);

	for (index = 0; index < tblsz; index += MAXPHYS / pp->sectorsize) {
		error = g_write_data(cp,
		    (table->lba[GPT_ELT_SECTBL] + index) * pp->sectorsize,
		    buf + (index + 1) * pp->sectorsize,
		    (tblsz - index > MAXPHYS / pp->sectorsize) ? MAXPHYS:
		    (tblsz - index) * pp->sectorsize);
		if (error)
			goto out;
	}
	error = g_write_data(cp, table->lba[GPT_ELT_SECHDR] * pp->sectorsize,
	    buf, pp->sectorsize);

 out:
	g_free(buf);
	return (error);
}

static void
g_gpt_set_defaults(struct g_part_table *basetable, struct g_provider *pp)
{
	struct g_part_entry *baseentry;
	struct g_part_gpt_entry *entry;
	struct g_part_gpt_table *table;
	quad_t start, end, min, max;
	quad_t lba, last;
	size_t spb, tblsz;

	table = (struct g_part_gpt_table *)basetable;
	last = pp->mediasize / pp->sectorsize - 1;
	tblsz = howmany(basetable->gpt_entries * sizeof(struct gpt_ent),
	    pp->sectorsize);

	table->lba[GPT_ELT_PRIHDR] = 1;
	table->lba[GPT_ELT_PRITBL] = 2;
	table->lba[GPT_ELT_SECHDR] = last;
	table->lba[GPT_ELT_SECTBL] = last - tblsz;
	table->state[GPT_ELT_PRIHDR] = GPT_STATE_OK;
	table->state[GPT_ELT_PRITBL] = GPT_STATE_OK;
	table->state[GPT_ELT_SECHDR] = GPT_STATE_OK;
	table->state[GPT_ELT_SECTBL] = GPT_STATE_OK;

	max = start = 2 + tblsz;
	min = end = last - tblsz - 1;
	LIST_FOREACH(baseentry, &basetable->gpt_entry, gpe_entry) {
		if (baseentry->gpe_deleted)
			continue;
		entry = (struct g_part_gpt_entry *)baseentry;
		if (entry->ent.ent_lba_start < min)
			min = entry->ent.ent_lba_start;
		if (entry->ent.ent_lba_end > max)
			max = entry->ent.ent_lba_end;
	}
	spb = 4096 / pp->sectorsize;
	if (spb > 1) {
		lba = start + ((start % spb) ? spb - start % spb : 0);
		if (lba <= min)
			start = lba;
		lba = end - (end + 1) % spb;
		if (max <= lba)
			end = lba;
	}
	table->hdr->hdr_lba_start = start;
	table->hdr->hdr_lba_end = end;

	basetable->gpt_first = start;
	basetable->gpt_last = end;
}

static void
g_gpt_printf_utf16(struct sbuf *sb, uint16_t *str, size_t len)
{
	u_int bo;
	uint32_t ch;
	uint16_t c;

	bo = LITTLE_ENDIAN;	/* GPT is little-endian */
	while (len > 0 && *str != 0) {
		ch = (bo == BIG_ENDIAN) ? be16toh(*str) : le16toh(*str);
		str++, len--;
		if ((ch & 0xf800) == 0xd800) {
			if (len > 0) {
				c = (bo == BIG_ENDIAN) ? be16toh(*str)
				    : le16toh(*str);
				str++, len--;
			} else
				c = 0xfffd;
			if ((ch & 0x400) == 0 && (c & 0xfc00) == 0xdc00) {
				ch = ((ch & 0x3ff) << 10) + (c & 0x3ff);
				ch += 0x10000;
			} else
				ch = 0xfffd;
		} else if (ch == 0xfffe) { /* BOM (U+FEFF) swapped. */
			bo = (bo == BIG_ENDIAN) ? LITTLE_ENDIAN : BIG_ENDIAN;
			continue;
		} else if (ch == 0xfeff) /* BOM (U+FEFF) unswapped. */
			continue;

		/* Write the Unicode character in UTF-8 */
		if (ch < 0x80)
			g_conf_printf_escaped(sb, "%c", ch);
		else if (ch < 0x800)
			g_conf_printf_escaped(sb, "%c%c", 0xc0 | (ch >> 6),
			    0x80 | (ch & 0x3f));
		else if (ch < 0x10000)
			g_conf_printf_escaped(sb, "%c%c%c", 0xe0 | (ch >> 12),
			    0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
		else if (ch < 0x200000)
			g_conf_printf_escaped(sb, "%c%c%c%c", 0xf0 |
			    (ch >> 18), 0x80 | ((ch >> 12) & 0x3f),
			    0x80 | ((ch >> 6) & 0x3f), 0x80 | (ch & 0x3f));
	}
}

static void
g_gpt_utf8_to_utf16(const uint8_t *s8, uint16_t *s16, size_t s16len)
{
	size_t s16idx, s8idx;
	uint32_t utfchar;
	unsigned int c, utfbytes;

	s8idx = s16idx = 0;
	utfchar = 0;
	utfbytes = 0;
	bzero(s16, s16len << 1);
	while (s8[s8idx] != 0 && s16idx < s16len) {
		c = s8[s8idx++];
		if ((c & 0xc0) != 0x80) {
			/* Initial characters. */
			if (utfbytes != 0) {
				/* Incomplete encoding of previous char. */
				s16[s16idx++] = htole16(0xfffd);
			}
			if ((c & 0xf8) == 0xf0) {
				utfchar = c & 0x07;
				utfbytes = 3;
			} else if ((c & 0xf0) == 0xe0) {
				utfchar = c & 0x0f;
				utfbytes = 2;
			} else if ((c & 0xe0) == 0xc0) {
				utfchar = c & 0x1f;
				utfbytes = 1;
			} else {
				utfchar = c & 0x7f;
				utfbytes = 0;
			}
		} else {
			/* Followup characters. */
			if (utfbytes > 0) {
				utfchar = (utfchar << 6) + (c & 0x3f);
				utfbytes--;
			} else if (utfbytes == 0)
				utfbytes = ~0;
		}
		/*
		 * Write the complete Unicode character as UTF-16 when we
		 * have all the UTF-8 charactars collected.
		 */
		if (utfbytes == 0) {
			/*
			 * If we need to write 2 UTF-16 characters, but
			 * we only have room for 1, then we truncate the
			 * string by writing a 0 instead.
			 */
			if (utfchar >= 0x10000 && s16idx < s16len - 1) {
				s16[s16idx++] =
				    htole16(0xd800 | ((utfchar >> 10) - 0x40));
				s16[s16idx++] =
				    htole16(0xdc00 | (utfchar & 0x3ff));
			} else
				s16[s16idx++] = (utfchar >= 0x10000) ? 0 :
				    htole16(utfchar);
		}
	}
	/*
	 * If our input string was truncated, append an invalid encoding
	 * character to the output string.
	 */
	if (utfbytes != 0 && s16idx < s16len)
		s16[s16idx++] = htole16(0xfffd);
}
